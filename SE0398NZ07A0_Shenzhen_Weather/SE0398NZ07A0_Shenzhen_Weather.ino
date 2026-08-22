#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>

#include "secrets.h"
#include "weather_font.h"
#include "weather_icons.h"

extern "C" {
#include <user_interface.h>
}

namespace {

// Waveshare e-Paper ESP8266 Driver Board (old-version mapping from EspInfo).
constexpr uint8_t kPinCs = D8;     // GPIO15 -> EPD CS
constexpr uint8_t kPinDc = D2;     // GPIO4  -> EPD DC
constexpr uint8_t kPinRst = D1;    // GPIO5  -> EPD RST
constexpr uint8_t kPinBusy = D0;   // GPIO16 <- EPD BUSY
constexpr uint8_t kPinButton = D3; // GPIO0  <- board FLASH button (active LOW)
constexpr uint16_t kPanelWidth = 768;
constexpr uint16_t kPanelHeight = 552;
constexpr uint16_t kHalfHeight = kPanelHeight / 2;
constexpr size_t kRowBytes = kPanelWidth / 4;
constexpr uint32_t kSpiFrequencyHz = 1000000;
constexpr uint32_t kResetTimeoutMs = 10000;
constexpr uint32_t kBusyAssertTimeoutMs = 2000;
constexpr uint32_t kPowerTimeoutMs = 60000;
constexpr uint32_t kRefreshTimeoutMs = 180000;
constexpr uint32_t kWifiTimeoutMs = 20000;
constexpr uint32_t kUpdateIntervalMs = 3600000;
constexpr uint32_t kButtonDebounceMs = 40;
constexpr uint32_t kLongPressMs = 1200;
constexpr uint8_t kHourlyPoints = 8;
constexpr uint8_t kForecastDays = 7;
// The verified A0 row writer already has the panel's native left-to-right
// order. Keep the logical weather and stock layouts unmirrored.
constexpr bool kMirrorWeather = false;

constexpr uint16_t kChartLeft = 316;
constexpr uint16_t kChartRight = 752;
constexpr uint16_t kChartTop = 52;
constexpr uint16_t kChartBottom = 310;
constexpr uint16_t kChartPlotLeft = 340;
constexpr uint16_t kChartPlotRight = 730;
constexpr int16_t kChartTemperatureY = 94;
constexpr int16_t kChartIconY = 120;
constexpr int16_t kChartPlotTop = 158;
constexpr int16_t kChartPlotBottom = 247;
constexpr int16_t kChartTimeYEven = 264;
constexpr int16_t kChartTimeYOdd = 288;
static_assert(16 + (kForecastDays * 736) / kForecastDays == 752,
              "Forecast strip must align with the main frame");

enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };

struct WeatherData {
  float temperature = 0;
  float apparent = 0;
  int humidity = 0;
  int currentCode = 3;
  float high[kForecastDays] = {0};
  float low[kForecastDays] = {0};
  int rain[kForecastDays] = {0};
  int codes[kForecastDays] = {3};
  char dates[kForecastDays][11] = {{0}};
  char sunrise[kForecastDays][6] = {{0}};
  char sunset[kForecastDays][6] = {{0}};
  char currentDate[11] = "----------";
  float hourlyTemperature[kHourlyPoints] = {0};
  int hourlyCodes[kHourlyPoints] = {3};
  char hourlyTime[kHourlyPoints][6] = {{0}};
  uint8_t hourlyCount = 0;
  bool valid = false;
  char updated[6] = "--:--";
};

struct StockData {
  float price = 0;
  float previous = 0;
  float change = 0;
  float changePercent = 0;
  float open = 0;
  float high = 0;
  float low = 0;
  long volume = 0;
  bool valid = false;
  char updated[12] = "--:--";
};

enum Page : uint8_t { WEATHER_PAGE = 0, STOCK_PAGE = 1 };

SPISettings gSpi(kSpiFrequencyHz, MSBFIRST, SPI_MODE0);
WeatherData gWeather;
StockData gStock;
uint8_t gRowBuffer[kRowBytes];
uint32_t gLastUpdate = 0;
Page gPage = WEATHER_PAGE;
char gIpText[24] = "IP NO WIFI";

void writeCommand(uint8_t command) {
  SPI.beginTransaction(gSpi);
  digitalWrite(kPinDc, LOW);
  digitalWrite(kPinCs, LOW);
  SPI.transfer(command);
  digitalWrite(kPinCs, HIGH);
  SPI.endTransaction();
}

void writeData(const uint8_t* data, size_t length) {
  SPI.beginTransaction(gSpi);
  digitalWrite(kPinDc, HIGH);
  for (size_t i = 0; i < length; ++i) {
    digitalWrite(kPinCs, LOW);
    SPI.transfer(data[i]);
    digitalWrite(kPinCs, HIGH);
  }
  SPI.endTransaction();
}

void writeDataByte(uint8_t data) { writeData(&data, 1); }

bool waitReady(const __FlashStringHelper* phase, uint32_t timeout) {
  const uint32_t start = millis();
  while (digitalRead(kPinBusy) == LOW) {
    if (millis() - start >= timeout) {
      Serial.print(F("BUSY timeout: "));
      Serial.println(phase);
      return false;
    }
    delay(10);
    yield();
  }
  return true;
}

bool waitCycle(const __FlashStringHelper* phase, uint32_t readyTimeout) {
  const uint32_t start = millis();
  while (digitalRead(kPinBusy) == HIGH) {
    if (millis() - start >= kBusyAssertTimeoutMs) {
      Serial.print(F("BUSY did not assert: "));
      Serial.println(phase);
      return false;
    }
    delay(1);
    yield();
  }
  return waitReady(phase, readyTimeout);
}

bool initializePanel() {
  pinMode(kPinBusy, INPUT_PULLUP);
  Serial.print(F("BUSY before reset: "));
  Serial.println(digitalRead(kPinBusy) == HIGH ? F("HIGH") : F("LOW"));
  digitalWrite(kPinCs, HIGH);
  digitalWrite(kPinRst, HIGH);
  delay(20);
  digitalWrite(kPinRst, LOW);
  delay(40);
  digitalWrite(kPinRst, HIGH);
  delay(50);
  if (!waitReady(F("reset"), kResetTimeoutMs)) return false;

  writeCommand(0x00);
  writeDataByte(0x0B);
  const uint8_t resolution[] = {0x03, 0x00, 0x02, 0x28};
  writeCommand(0x61);
  writeData(resolution, sizeof(resolution));
  return true;
}

uint16_t physicalRow(uint16_t logicalRow) {
  if (logicalRow < kHalfHeight) return logicalRow * 2;
  return (kPanelHeight - 1) - 2 * (logicalRow - kHalfHeight);
}

void setWindow(uint16_t firstRow, uint16_t lastRow) {
  const uint8_t window[] = {0x00, 0x00, 0x02, 0xFF,
                            static_cast<uint8_t>(firstRow >> 8),
                            static_cast<uint8_t>(firstRow),
                            static_cast<uint8_t>(lastRow >> 8),
                            static_cast<uint8_t>(lastRow), 0x01};
  writeCommand(0x83);
  writeData(window, sizeof(window));
}

bool refreshPanel() {
  setWindow(0, kPanelHeight - 1);
  writeCommand(0x04);
  if (!waitCycle(F("power on"), kPowerTimeoutMs)) return false;
  writeCommand(0x12);
  writeDataByte(0x00);
  if (!waitCycle(F("refresh"), kRefreshTimeoutMs)) return false;
  writeCommand(0x02);
  writeDataByte(0x00);
  if (!waitCycle(F("power off"), kPowerTimeoutMs)) return false;
  writeCommand(0x07);
  writeDataByte(0xA5);
  return true;
}

int findJsonKey(const String& json, const char* key, int start = 0) {
  const String quoted = String("\"") + key + "\"";
  return json.indexOf(quoted, start);
}

float jsonNumber(const String& json, const char* key, int start = 0) {
  int pos = findJsonKey(json, key, start);
  if (pos < 0) return NAN;
  pos = json.indexOf(':', pos);
  if (pos < 0) return NAN;
  return json.substring(pos + 1).toFloat();
}

float jsonArrayNumber(const String& json, const char* key, int index) {
  int pos = findJsonKey(json, key);
  if (pos < 0) return NAN;
  pos = json.indexOf('[', pos);
  if (pos < 0) return NAN;
  for (int i = 0; i < index; ++i) {
    pos = json.indexOf(',', pos + 1);
    if (pos < 0) return NAN;
  }
  return json.substring(pos + 1).toFloat();
}

float jsonArrayNumberFrom(const String& json, int arrayStart, int index) {
  if (arrayStart < 0) return NAN;
  int pos = arrayStart;
  for (int i = 0; i < index; ++i) {
    pos = json.indexOf(',', pos + 1);
    if (pos < 0) return NAN;
  }
  return json.substring(pos + 1).toFloat();
}

bool jsonArrayString(const String& json, const char* key, int index,
                     char* output, size_t outputSize) {
  int pos = findJsonKey(json, key);
  if (pos < 0) return false;
  pos = json.indexOf('[', pos);
  if (pos < 0) return false;
  for (int i = 0; i < index; ++i) {
    pos = json.indexOf(',', pos + 1);
    if (pos < 0) return false;
  }
  int first = json.indexOf('"', pos);
  if (first < 0) return false;
  int last = json.indexOf('"', first + 1);
  if (last < 0) return false;
  size_t length = min(outputSize - 1, static_cast<size_t>(last - first - 1));
  memcpy(output, json.c_str() + first + 1, length);
  output[length] = 0;
  return true;
}

bool jsonArrayStringFrom(const String& json, int arrayStart, int index,
                         char* output, size_t outputSize) {
  if (arrayStart < 0) return false;
  int pos = arrayStart;
  for (int i = 0; i < index; ++i) {
    pos = json.indexOf(',', pos + 1);
    if (pos < 0) return false;
  }
  const int first = json.indexOf('"', pos);
  if (first < 0) return false;
  const int last = json.indexOf('"', first + 1);
  if (last < 0) return false;
  const size_t length = min(outputSize - 1,
                            static_cast<size_t>(last - first - 1));
  memcpy(output, json.c_str() + first + 1, length);
  output[length] = 0;
  return true;
}

bool jsonString(const String& json, const char* key, char* output,
                size_t outputSize) {
  int pos = findJsonKey(json, key);
  if (pos < 0) return false;
  pos = json.indexOf('"', json.indexOf(':', pos) + 1);
  if (pos < 0) return false;
  const int end = json.indexOf('"', pos + 1);
  if (end < 0) return false;
  const size_t length = min(outputSize - 1,
                            static_cast<size_t>(end - pos - 1));
  memcpy(output, json.c_str() + pos + 1, length);
  output[length] = 0;
  return true;
}

bool stockField(const String& body, int index, char* output, size_t outputSize) {
  int pos = body.indexOf('=');
  if (pos < 0) return false;
  ++pos;
  if (body[pos] == '"') ++pos;
  for (int i = 0; i < index; ++i) {
    pos = body.indexOf('~', pos);
    if (pos < 0) return false;
    ++pos;
  }
  int end = body.indexOf('~', pos);
  const int quote = body.indexOf('"', pos);
  if (end < 0 || (quote >= 0 && quote < end)) end = quote;
  if (end <= pos) return false;
  const size_t length = min(outputSize - 1,
                            static_cast<size_t>(end - pos));
  memcpy(output, body.c_str() + pos, length);
  output[length] = 0;
  return true;
}

float stockNumber(const String& body, int index) {
  char value[20] = {0};
  if (!stockField(body, index, value, sizeof(value))) return NAN;
  return atof(value);
}

const char* weatherText(int code) {
  if (code == 0) return "晴";
  if (code <= 2) return "多云";
  if (code == 3) return "阴";
  if (code == 45 || code == 48) return "雾";
  if (code <= 55) return "小雨";
  if (code <= 65 || code == 80 || code == 81) return "中雨";
  if (code >= 71 && code <= 77) return "雪";
  if (code == 82) return "大雨";
  return "雷雨";
}

uint8_t weekdayFromIsoDate(const char* date) {
  int year = 0;
  int month = 0;
  int day = 0;
  if (!date || sscanf(date, "%4d-%2d-%2d", &year, &month, &day) != 3 ||
      month < 1 || month > 12 || day < 1 || day > 31) {
    return 0;
  }
  static const uint8_t offsets[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (month < 3) --year;
  return static_cast<uint8_t>((year + year / 4 - year / 100 + year / 400 +
                               offsets[month - 1] + day) % 7);
}

const char* weekdayLabel(const char* date) {
  static const char* const labels[] = {
      "周日", "周一", "周二", "周三", "周四", "周五", "周六"};
  return labels[weekdayFromIsoDate(date)];
}

bool fetchWeather() {
  Serial.println(F("Connecting WiFi..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPassword);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    delay(250);
    yield();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi connection failed."));
    snprintf(gIpText, sizeof(gIpText), "IP NO WIFI");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  const IPAddress localIp = WiFi.localIP();
  snprintf(gIpText, sizeof(gIpText), "IP %u.%u.%u.%u", localIp[0], localIp[1],
           localIp[2], localIp[3]);
  Serial.print(F("WiFi connected, IP: "));
  Serial.println(gIpText + 3);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
      new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setBufferSizes(512, 512);
  HTTPClient http;
  const char* url =
      "https://api.open-meteo.com/v1/forecast?latitude=22.5431&longitude=114.0579&timezone=Asia%2FShanghai&forecast_days=8&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code&hourly=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,sunrise,sunset";
  bool ok = false;
  if (http.begin(*client, url)) {
    http.setTimeout(20000);
    const int status = http.GET();
    if (status == HTTP_CODE_OK) {
      const String body = http.getString();
      DynamicJsonDocument doc(12288);
      const DeserializationError parseError = deserializeJson(doc, body);
      if (parseError) {
        Serial.print(F("Weather JSON error: "));
        Serial.println(parseError.c_str());
      } else {
        WeatherData next;
        const JsonObject current = doc["current"].as<JsonObject>();
        const JsonObject daily = doc["daily"].as<JsonObject>();
        const JsonObject hourly = doc["hourly"].as<JsonObject>();
        next.temperature = current["temperature_2m"] | NAN;
        next.apparent = current["apparent_temperature"] | NAN;
        next.humidity = current["relative_humidity_2m"] | -1;
        next.currentCode = current["weather_code"] | 3;

        for (uint8_t day = 0; day < kForecastDays; ++day) {
          const uint8_t sourceDay = day + 1;
          next.high[day] = daily["temperature_2m_max"][sourceDay] | NAN;
          next.low[day] = daily["temperature_2m_min"][sourceDay] | NAN;
          next.rain[day] =
              daily["precipitation_probability_max"][sourceDay] | -1;
          next.codes[day] = daily["weather_code"][sourceDay] | 3;
          const char* date = daily["time"][sourceDay] | "";
          snprintf(next.dates[day], sizeof(next.dates[day]), "%s", date);
          const char* sunrise = daily["sunrise"][sourceDay] | "";
          const char* sunset = daily["sunset"][sourceDay] | "";
          snprintf(next.sunrise[day], sizeof(next.sunrise[day]), "%.5s",
                   sunrise);
          snprintf(next.sunset[day], sizeof(next.sunset[day]), "%.5s",
                   sunset);
        }

        const char* currentTime = current["time"] | "";
        if (strlen(currentTime) >= 16) {
          snprintf(next.currentDate, sizeof(next.currentDate), "%.10s",
                   currentTime);
        }
        const JsonArray hourlyTimes = hourly["time"].as<JsonArray>();
        const JsonArray hourlyTemperatures =
            hourly["temperature_2m"].as<JsonArray>();
        const JsonArray hourlyCodes = hourly["weather_code"].as<JsonArray>();
        size_t firstHour = 0;
        for (size_t i = 0; i < hourlyTimes.size(); ++i) {
          const char* time = hourlyTimes[i] | "";
          if (strcmp(time, currentTime) >= 0) {
            firstHour = i;
            break;
          }
        }
        for (uint8_t i = 0; i < kHourlyPoints; ++i) {
          const size_t source = firstHour + i;
          if (source >= hourlyTemperatures.size() ||
              source >= hourlyCodes.size() ||
              source >= hourlyTimes.size()) break;
          next.hourlyTemperature[i] = hourlyTemperatures[source] | NAN;
          next.hourlyCodes[i] = hourlyCodes[source] | -1;
          const char* time = hourlyTimes[source] | "";
          if (strlen(time) >= 16) {
            snprintf(next.hourlyTime[i], sizeof(next.hourlyTime[i]), "%c%c:%c%c",
                     time[11], time[12], time[14], time[15]);
          }
          if (isnan(next.hourlyTemperature[i]) || next.hourlyCodes[i] < 0) break;
          next.hourlyCount = i + 1;
        }

        bool dailyValid = next.currentDate[0] != '-';
        for (uint8_t day = 0; day < kForecastDays; ++day) {
          dailyValid = dailyValid && next.dates[day][0] != 0 &&
                       !isnan(next.high[day]) && !isnan(next.low[day]) &&
                       next.rain[day] >= 0 && next.codes[day] >= 0;
        }
        next.valid = dailyValid && !isnan(next.temperature) &&
                     next.temperature > -100.0f &&
                     next.temperature < 100.0f && next.humidity >= 0 &&
                     next.hourlyCount == kHourlyPoints;
        if (next.valid) {
          gWeather = next;
          if (strlen(currentTime) >= 16) {
            snprintf(gWeather.updated, sizeof(gWeather.updated), "%c%c:%c%c",
                     currentTime[11], currentTime[12], currentTime[14],
                     currentTime[15]);
          }
          Serial.printf("Weather values: %.1fC, humidity %d%%, code %d, hourly %u\n",
                        gWeather.temperature, gWeather.humidity,
                        gWeather.currentCode, gWeather.hourlyCount);
          ok = true;
        } else {
          Serial.println(F("Weather response missing valid current data."));
        }
      }
    } else {
      Serial.printf("HTTP status: %d\n", status);
    }
    http.end();
  }
  pinMode(kPinBusy, INPUT_PULLUP);
  delay(200);
  Serial.println(ok ? F("Weather updated.") : F("Weather parse failed."));
  return ok;
}

bool fetchStock() {
  Serial.println(F("Fetching BYD stock quote..."));
  WiFi.mode(WIFI_STA);
  WiFi.begin(kWifiSsid, kWifiPassword);
  const uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < kWifiTimeoutMs) {
    delay(250);
    yield();
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi connection failed."));
    snprintf(gIpText, sizeof(gIpText), "IP NO WIFI");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  const IPAddress localIp = WiFi.localIP();
  snprintf(gIpText, sizeof(gIpText), "IP %u.%u.%u.%u", localIp[0], localIp[1],
           localIp[2], localIp[3]);
  Serial.print(F("WiFi connected, IP: "));
  Serial.println(gIpText + 3);

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
      new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setBufferSizes(512, 512);
  HTTPClient http;
  bool ok = false;
  if (http.begin(*client, "https://qt.gtimg.cn/q=sz002594")) {
    http.setTimeout(20000);
    http.addHeader("User-Agent", "Mozilla/5.0");
    const int status = http.GET();
    if (status == HTTP_CODE_OK) {
      const String body = http.getString();
      StockData next;
      next.price = stockNumber(body, 2);
      next.previous = stockNumber(body, 3);
      next.open = stockNumber(body, 4);
      next.change = stockNumber(body, 30);
      next.changePercent = stockNumber(body, 31);
      next.high = stockNumber(body, 32);
      next.low = stockNumber(body, 33);
      next.volume = static_cast<long>(stockNumber(body, 5));
      char quoteTime[16] = {0};
      if (stockField(body, 29, quoteTime, sizeof(quoteTime))) {
        strncpy(next.updated, quoteTime, sizeof(next.updated) - 1);
        next.updated[sizeof(next.updated) - 1] = 0;
      }
      next.valid = !isnan(next.price) && next.price > 0 &&
                   !isnan(next.changePercent);
      if (next.valid) {
        gStock = next;
        ok = true;
      }
    } else {
      Serial.printf("Stock HTTP status: %d\n", status);
    }
    http.end();
  }
  pinMode(kPinBusy, INPUT_PULLUP);
  Serial.println(ok ? F("BYD quote updated.") : F("BYD quote parse failed."));
  return ok;
}

uint8_t cjkSourceColumn(uint8_t displayColumn, uint8_t glyphWidth) {
  return displayColumn < glyphWidth ? displayColumn : 0;
}

uint8_t asciiSourceColumn(uint8_t displayColumn, uint8_t glyphWidth) {
  return displayColumn < glyphWidth ? displayColumn : 0;
}

bool glyphPixel(uint32_t codepoint, uint8_t x, uint8_t y) {
  if (x >= 16 || y >= 16) return false;
  for (uint8_t i = 0; i < kWeatherFontCount; ++i) {
    const uint32_t stored = pgm_read_dword(&kWeatherFont[i].codepoint);
    if (stored == codepoint) {
      const uint8_t bits = pgm_read_byte(
          &kWeatherFont[i].bitmap[y * 2 + x / 8]);
      return bits & (1 << (7 - (x & 7)));
    }
  }
  return false;
}

uint32_t nextUtf8(const char*& text) {
  const uint8_t first = static_cast<uint8_t>(*text++);
  if (first < 0x80) return first;
  if ((first & 0xE0) == 0xC0) {
    const uint8_t second = static_cast<uint8_t>(*text++);
    return ((first & 0x1F) << 6) | (second & 0x3F);
  }
  const uint8_t second = static_cast<uint8_t>(*text++);
  const uint8_t third = static_cast<uint8_t>(*text++);
  return ((first & 0x0F) << 12) | ((second & 0x3F) << 6) | (third & 0x3F);
}

const uint8_t* asciiGlyph(char c) {
  static const uint8_t digits[][7] = {
      {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E},
      {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F},
      {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
      {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
      {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
      {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
      {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E},
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08},
      {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
      {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x1C}};
  static const uint8_t minus[] = {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00};
  static const uint8_t dot[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C};
  static const uint8_t colon[] = {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x0C, 0x00};
  static const uint8_t slash[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x00, 0x00};
  static const uint8_t celsius[] = {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E};
  static const uint8_t plus[] = {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00};
  static const uint8_t percent[] = {0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13};
  static const uint8_t letters[][7] = {
      {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // A
      {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E},  // B
      {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E},  // C
      {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E},  // D
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F},  // E
      {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10},  // F
      {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F},  // G
      {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11},  // H
      {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F},  // I
      {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E},  // J
      {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11},  // K
      {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F},  // L
      {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11},  // M
      {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11},  // N
      {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // O
      {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10},  // P
      {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D},  // Q
      {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11},  // R
      {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E},  // S
      {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04},  // T
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},  // U
      {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04},  // V
      {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A},  // W
      {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11},  // X
      {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04},  // Y
      {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F},  // Z
  };
  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c == '-') return minus;
  if (c == '.') return dot;
  if (c == ':') return colon;
  if (c == '/') return slash;
  if (c == 'C') return celsius;
  if (c == '+') return plus;
  if (c == '%') return percent;
  if (c >= 'A' && c <= 'Z') return letters[c - 'A'];
  return nullptr;
}

void setPixel(uint16_t x, Color color) {
  if (x >= kPanelWidth) return;
  // Keep panel coordinates stable; text glyphs handle their own column order.
  const bool mirror = gPage == WEATHER_PAGE && kMirrorWeather;
  const uint16_t physicalX = mirror ? (kPanelWidth - 1 - x) : x;
  const uint16_t byte = physicalX / 4;
  const uint8_t shift = 6 - (physicalX & 3) * 2;
  gRowBuffer[byte] = (gRowBuffer[byte] & ~(0x03 << shift)) |
                      (static_cast<uint8_t>(color) << shift);
}

void fillRow(Color color) {
  const uint8_t packed = (static_cast<uint8_t>(color) << 6) |
                         (static_cast<uint8_t>(color) << 4) |
                         (static_cast<uint8_t>(color) << 2) |
                         static_cast<uint8_t>(color);
  memset(gRowBuffer, packed, sizeof(gRowBuffer));
}

void drawRectRow(uint16_t row, uint16_t top, uint16_t bottom, uint16_t left,
                 uint16_t right, Color color) {
  if (row < top || row > bottom) return;
  if (row == top || row == bottom) {
    for (uint16_t x = left; x <= right; ++x) setPixel(x, color);
  } else {
    setPixel(left, color);
    setPixel(right, color);
  }
}

void drawCjkRow(uint16_t row, uint16_t x, uint16_t y, uint8_t scale,
               const char* text, Color color) {
  const uint16_t glyphHeight = 16 * scale;
  if (row < y || row >= y + glyphHeight) return;
  const uint8_t gy = (row - y) / scale;
  uint16_t cursor = x;
  const char* p = text;
  while (*p) {
    const uint32_t codepoint = nextUtf8(p);
    if (codepoint < 128) {
      cursor += 8 * scale;
      continue;
    }
    for (uint8_t gx = 0; gx < 16; ++gx) {
      if (!glyphPixel(codepoint, cjkSourceColumn(gx, 16), gy)) continue;
      for (uint8_t sx = 0; sx < scale; ++sx) setPixel(cursor + gx * scale + sx, color);
    }
    cursor += 17 * scale;
  }
}

void drawAsciiRow(uint16_t row, uint16_t x, uint16_t y, uint8_t scale,
                  const char* text, Color color) {
  if (row < y || row >= y + 7 * scale) return;
  const uint8_t gy = (row - y) / scale;
  uint16_t cursor = x;
  for (const char* p = text; *p; ++p) {
    const uint8_t* glyph = asciiGlyph(*p);
    if (glyph) {
      for (uint8_t gx = 0; gx < 5; ++gx) {
        const uint8_t sourceColumn = asciiSourceColumn(gx, 5);
        if (!(glyph[gy] & (1 << (4 - sourceColumn)))) continue;
        for (uint8_t sx = 0; sx < scale; ++sx) setPixel(cursor + gx * scale + sx, color);
      }
    }
    cursor += 6 * scale;
  }
}

uint16_t asciiTextWidth(const char* text, uint8_t scale) {
  return strlen(text) * 6 * scale;
}

uint16_t cjkTextWidth(const char* text, uint8_t scale) {
  uint16_t width = 0;
  const char* p = text;
  while (*p) {
    const uint32_t codepoint = nextUtf8(p);
    width += (codepoint < 128 ? 8 : 17) * scale;
  }
  return width > 0 ? width - scale : 0;
}

void drawAsciiCenteredRow(uint16_t row, uint16_t center, uint16_t y,
                          uint8_t scale, const char* text, Color color) {
  const uint16_t width = asciiTextWidth(text, scale);
  drawAsciiRow(row, center > width / 2 ? center - width / 2 : 0, y, scale,
               text, color);
}

void drawAsciiRightAlignedRow(uint16_t row, uint16_t right, uint16_t y,
                              uint8_t scale, const char* text, Color color) {
  const uint16_t width = asciiTextWidth(text, scale);
  drawAsciiRow(row, right > width ? right - width : 0, y, scale, text, color);
}

void drawCjkCenteredRow(uint16_t row, uint16_t center, uint16_t y,
                        uint8_t scale, const char* text, Color color) {
  const uint16_t width = cjkTextWidth(text, scale);
  drawCjkRow(row, center > width / 2 ? center - width / 2 : 0, y, scale,
             text, color);
}

void drawDegreeRow(uint16_t row, uint16_t x, uint16_t y, uint8_t scale,
                   Color color) {
  const uint8_t radius = max<uint8_t>(1, scale);
  const int16_t centerY = y + radius;
  if (row == centerY - radius || row == centerY + radius) {
    for (int16_t dx = -radius; dx <= radius; ++dx) {
      setPixel(x + radius + dx, color);
    }
  } else if (row > centerY - radius && row < centerY + radius) {
    setPixel(x, color);
    setPixel(x + radius * 2, color);
  }
}

uint16_t temperatureTextWidth(float value, uint8_t scale) {
  char number[8];
  snprintf(number, sizeof(number), "%.0f", value);
  return asciiTextWidth(number, scale) + 2 * scale + 1;
}

void drawTemperatureRow(uint16_t row, uint16_t x, uint16_t y, uint8_t scale,
                        float value, Color color) {
  char number[8];
  snprintf(number, sizeof(number), "%.0f", value);
  drawAsciiRow(row, x, y, scale, number, color);
  drawDegreeRow(row, x + asciiTextWidth(number, scale), y, scale, color);
}

void drawTemperatureCenteredRow(uint16_t row, uint16_t center, uint16_t y,
                                uint8_t scale, float value, Color color) {
  const uint16_t width = temperatureTextWidth(value, scale);
  drawTemperatureRow(row, center > width / 2 ? center - width / 2 : 0, y,
                     scale, value, color);
}

const uint8_t* weatherIconForCode(int code, Color& color) {
  if (code == 0) {
    color = YELLOW;
    return kWeatherIconSunny;
  }
  if (code <= 3) {
    color = YELLOW;
    return code <= 2 ? kWeatherIconPartlyCloudy : kWeatherIconCloudy;
  }
  if (code == 45 || code == 48) {
    color = YELLOW;
    return kWeatherIconFog;
  }
  if (code >= 71 && code <= 77) {
    color = YELLOW;
    return kWeatherIconSnow;
  }
  if (code >= 95) {
    color = RED;
    return kWeatherIconStorm;
  }
  color = RED;
  return code == 82 || (code >= 65 && code <= 67) ? kWeatherIconHeavyRain
                                                   : kWeatherIconRain;
}

void drawIconRow(uint16_t row, uint16_t left, uint16_t top, int code) {
  if (row < top || row >= top + kWeatherIconHeight) return;
  Color color = BLACK;
  const uint8_t* icon = weatherIconForCode(code, color);
  const uint16_t iconRow = row - top;
  for (uint16_t x = 0; x < kWeatherIconWidth; ++x) {
    const uint8_t packed = pgm_read_byte(
        icon + iconRow * kWeatherIconBytesPerRow + x / 8);
    if (packed & (0x80 >> (x & 7))) setPixel(left + x, color);
  }
}

void drawIconScaledRow(uint16_t row, uint16_t left, uint16_t top,
                       uint16_t width, uint16_t height, int code) {
  if (row < top || row >= top + height) return;
  Color color = BLACK;
  const uint8_t* icon = weatherIconForCode(code, color);
  const uint16_t sourceY = (row - top) * kWeatherIconHeight / height;
  for (uint16_t x = 0; x < width; ++x) {
    const uint16_t sourceX = x * kWeatherIconWidth / width;
    const uint8_t packed = pgm_read_byte(
        icon + sourceY * kWeatherIconBytesPerRow + sourceX / 8);
    if (packed & (0x80 >> (sourceX & 7))) setPixel(left + x, color);
  }
}

int16_t hourlyChartY(float temperature, float minimum, float maximum) {
  if (maximum - minimum < 0.1f) {
    return (kChartPlotTop + kChartPlotBottom) / 2;
  }
  const float normalized = (temperature - minimum) / (maximum - minimum);
  return static_cast<int16_t>(kChartPlotBottom -
                              normalized * (kChartPlotBottom - kChartPlotTop));
}

void drawHourlyPointLabelRow(uint16_t row, uint8_t i, uint16_t x,
                             int16_t y) {
  drawTemperatureCenteredRow(row, x, kChartTemperatureY, 2,
                             gWeather.hourlyTemperature[i], BLACK);
  drawIconScaledRow(row, x - 14, kChartIconY, 28, 28,
                    gWeather.hourlyCodes[i]);

  const int16_t guideTop = kChartIconY + 31;
  const int16_t guideBottom = y - 7;
  if (row >= guideTop && row <= guideBottom &&
      (row - guideTop) % 4 < 2) {
    setPixel(x, BLACK);
  }

  const int16_t timeY = i % 2 == 0 ? kChartTimeYEven : kChartTimeYOdd;
  const uint16_t timeWidth = asciiTextWidth(gWeather.hourlyTime[i], 2);
  int16_t timeX = static_cast<int16_t>(x) - timeWidth / 2;
  timeX = max<int16_t>(kChartLeft + 2, timeX);
  timeX = min<int16_t>(kChartRight - timeWidth - 2, timeX);
  drawAsciiRow(row, timeX, timeY, 2, gWeather.hourlyTime[i], BLACK);

  if (row == y) {
    setPixel(x - 2, YELLOW);
    setPixel(x - 1, YELLOW);
    setPixel(x, YELLOW);
    setPixel(x + 1, YELLOW);
    setPixel(x + 2, YELLOW);
  } else if (abs(static_cast<int16_t>(row) - y) <= 2) {
    setPixel(x, YELLOW);
  }
}

uint16_t hourlyChartX(uint8_t index, uint8_t count) {
  if (count < 2) return kChartPlotLeft;
  return kChartPlotLeft +
         static_cast<uint16_t>(index * (kChartPlotRight - kChartPlotLeft) /
                               (count - 1));
}

void drawHourlyChartRow(uint16_t row) {
  drawRectRow(row, kChartTop, kChartBottom, kChartLeft, kChartRight, BLACK);
  drawCjkRow(row, 336, 56, 2, "近", BLACK);
  drawAsciiRow(row, 370, 56, 2, "8", BLACK);
  drawCjkRow(row, 384, 56, 2, "小时天气", BLACK);
  if (gWeather.hourlyCount == 0) return;

  float minimum = gWeather.hourlyTemperature[0];
  float maximum = minimum;
  for (uint8_t i = 1; i < gWeather.hourlyCount; ++i) {
    minimum = min(minimum, gWeather.hourlyTemperature[i]);
    maximum = max(maximum, gWeather.hourlyTemperature[i]);
  }
  for (uint8_t i = 0; i + 1 < gWeather.hourlyCount; ++i) {
    const uint16_t x1 = hourlyChartX(i, gWeather.hourlyCount);
    const uint16_t x2 = hourlyChartX(i + 1, gWeather.hourlyCount);
    const int16_t y1 = hourlyChartY(gWeather.hourlyTemperature[i], minimum, maximum);
    const int16_t y2 = hourlyChartY(gWeather.hourlyTemperature[i + 1], minimum, maximum);
    for (uint16_t x = x1; x <= x2; ++x) {
      const int16_t y = y1 + static_cast<int16_t>(
          (static_cast<int32_t>(y2 - y1) * (x - x1)) / (x2 - x1));
      if (abs(static_cast<int16_t>(row) - y) <= 1) setPixel(x, RED);
    }
  }
  for (uint8_t i = 0; i < gWeather.hourlyCount; ++i) {
    const uint16_t x = hourlyChartX(i, gWeather.hourlyCount);
    const int16_t y = hourlyChartY(gWeather.hourlyTemperature[i], minimum, maximum);
    drawHourlyPointLabelRow(row, i, x, y);
  }
}

void drawWeatherRow(uint16_t row) {
  fillRow(WHITE);
  if (row < 6) {
    for (uint16_t x = 0; x < kPanelWidth; ++x) setPixel(x, RED);
  }
  drawAsciiRow(row, 22, 21, 2, gIpText, BLACK);
  drawAsciiCenteredRow(row, kPanelWidth / 2, 21, 2, gWeather.currentDate,
                       BLACK);
  drawCjkRow(row, 640, 20, 1, "更新", BLACK);
  drawAsciiRightAlignedRow(row, 746, 21, 2, gWeather.updated, BLACK);
  drawRectRow(row, 52, 310, 16, 752, BLACK);
  drawIconScaledRow(row, 36, 80, 84, 84, gWeather.currentCode);
  drawTemperatureRow(row, 145, 78, 4, gWeather.temperature, BLACK);
  drawCjkRow(row, 145, 142, 2, weatherText(gWeather.currentCode), BLACK);
  if (row == 210) {
    for (uint16_t x = 28; x <= 304; ++x) setPixel(x, BLACK);
  }
  drawCjkRow(row, 28, 258, 2, "体感", BLACK);
  drawTemperatureRow(row, 98, 266, 2, gWeather.apparent, BLACK);
  char humidity[12];
  snprintf(humidity, sizeof(humidity), "%d%%", gWeather.humidity);
  drawCjkRow(row, 158, 258, 2, "湿度", BLACK);
  drawAsciiRow(row, 234, 266, 2, humidity, BLACK);
  drawHourlyChartRow(row);

  for (uint8_t day = 0; day < kForecastDays; ++day) {
    const uint16_t left = 16 + (day * 736) / kForecastDays;
    const uint16_t right = 16 + ((day + 1) * 736) / kForecastDays;
    const uint16_t center = (left + right) / 2;
    drawRectRow(row, 318, 540, left, right, BLACK);
    drawCjkCenteredRow(row, center, 326, 2,
                       weekdayLabel(gWeather.dates[day]), BLACK);
    drawAsciiCenteredRow(row, center, 362, 2, gWeather.dates[day] + 5,
                         BLACK);
    const uint16_t iconLeft = center - 32;
    drawIconScaledRow(row, iconLeft, 390, 64, 64, gWeather.codes[day]);

    const uint16_t highWidth = temperatureTextWidth(gWeather.high[day], 2);
    const uint16_t lowWidth = temperatureTextWidth(gWeather.low[day], 2);
    const uint16_t slashWidth = asciiTextWidth("/", 2);
    const uint16_t temperaturesWidth = highWidth + slashWidth + lowWidth + 8;
    uint16_t temperatureX = center - temperaturesWidth / 2;
    drawTemperatureRow(row, temperatureX, 472, 2, gWeather.high[day], BLACK);
    temperatureX += highWidth + 4;
    drawAsciiRow(row, temperatureX, 472, 2, "/", BLACK);
    temperatureX += slashWidth + 4;
    drawTemperatureRow(row, temperatureX, 472, 2, gWeather.low[day], BLACK);

    char rain[12];
    snprintf(rain, sizeof(rain), "%d%%", gWeather.rain[day]);
    const uint16_t precipitationWidth =
        cjkTextWidth("降水", 1) + 4 + asciiTextWidth(rain, 2);
    const uint16_t precipitationX = center - precipitationWidth / 2;
    drawCjkRow(row, precipitationX, 508, 1, "降水", BLACK);
    drawAsciiRow(row, precipitationX + 37, 510, 2, rain, BLACK);
  }
}

void drawStockRow(uint16_t row) {
  fillRow(WHITE);
  if (row < 6) {
    for (uint16_t x = 0; x < kPanelWidth; ++x) setPixel(x, RED);
  }
  drawAsciiRow(row, 22, 18, 2, "BYD 002594", BLACK);
  drawAsciiRow(row, 630, 23, 2, "STOCK", BLACK);
  drawAsciiRow(row, 360, 21, 2, gIpText, BLACK);
  drawRectRow(row, 76, 274, 16, 752, BLACK);

  if (!gStock.valid) {
    drawAsciiRow(row, 260, 160, 4, "NO DATA", RED);
    return;
  }

  char value[20];
  snprintf(value, sizeof(value), "%.2f", gStock.price);
  drawAsciiRow(row, 42, 105, 7, value, BLACK);
  snprintf(value, sizeof(value), "%+.2f", gStock.change);
  drawAsciiRow(row, 430, 112, 4, value, gStock.change >= 0 ? RED : BLACK);
  snprintf(value, sizeof(value), "%+.2f%%", gStock.changePercent);
  drawAsciiRow(row, 430, 170, 3, value, gStock.changePercent >= 0 ? RED : BLACK);

  drawRectRow(row, 300, 530, 16, 752, BLACK);
  char line[28];
  snprintf(line, sizeof(line), "PREV %.2f", gStock.previous);
  drawAsciiRow(row, 34, 324, 2, line, BLACK);
  snprintf(line, sizeof(line), "OPEN %.2f", gStock.open);
  drawAsciiRow(row, 274, 324, 2, line, BLACK);
  snprintf(line, sizeof(line), "HIGH %.2f", gStock.high);
  drawAsciiRow(row, 514, 324, 2, line, RED);
  snprintf(line, sizeof(line), "LOW %.2f", gStock.low);
  drawAsciiRow(row, 34, 376, 2, line, BLACK);
  snprintf(line, sizeof(line), "VOL %ld", gStock.volume);
  drawAsciiRow(row, 274, 376, 2, line, BLACK);
  snprintf(line, sizeof(line), "UPDATED %s", gStock.updated);
  drawAsciiRow(row, 34, 428, 2, line, BLACK);

  const Color trendColor = gStock.changePercent >= 0 ? RED : BLACK;
  const uint16_t baseY = 505;
  const uint16_t magnitude = static_cast<uint16_t>(min(150.0f, fabs(gStock.changePercent) * 12.0f));
  if (row == baseY) {
    for (uint16_t x = 520; x < 724; ++x) setPixel(x, trendColor);
  }
  if (gStock.changePercent >= 0 && row >= baseY - 24 && row <= baseY) {
    for (uint16_t x = 620; x < 620 + magnitude; ++x) {
      setPixel(x, trendColor);
    }
  } else if (gStock.changePercent < 0 && row >= baseY && row <= baseY + 24) {
    for (uint16_t x = 620; x > 620 - magnitude; --x) {
      setPixel(x, trendColor);
    }
  }
}

void writeWeatherImage() {
  for (uint16_t row = 0; row < kPanelHeight; ++row) {
    drawWeatherRow(row);
    setWindow(physicalRow(row), physicalRow(row));
    writeCommand(0x10);
    writeData(gRowBuffer, sizeof(gRowBuffer));
    if (row % 50 == 0) {
      Serial.print(F("Weather row "));
      Serial.println(row);
    }
    yield();
  }
  Serial.println(F("Weather image written."));
}

void writeStockImage() {
  for (uint16_t row = 0; row < kPanelHeight; ++row) {
    drawStockRow(row);
    setWindow(physicalRow(row), physicalRow(row));
    writeCommand(0x10);
    writeData(gRowBuffer, sizeof(gRowBuffer));
    if (row % 50 == 0) {
      Serial.print(F("Stock row "));
      Serial.println(row);
    }
    yield();
  }
  Serial.println(F("Stock image written."));
}

bool updateAndDisplay() {
  const bool fetched = gPage == WEATHER_PAGE ? fetchWeather() : fetchStock();
  const bool valid = gPage == WEATHER_PAGE ? gWeather.valid : gStock.valid;
  if (!fetched && !valid) {
    Serial.println(F("No valid data; display skipped."));
    return false;
  }
  if (!initializePanel()) return false;
  if (gPage == WEATHER_PAGE) {
    writeWeatherImage();
  } else {
    writeStockImage();
  }
  const bool refreshed = refreshPanel();
  Serial.println(refreshed ? F("Display complete.") : F("Display failed."));
  return refreshed;
}

void handleFlashButton() {
  if (digitalRead(kPinButton) != LOW) return;
  delay(kButtonDebounceMs);
  if (digitalRead(kPinButton) != LOW) return;

  const uint32_t pressedAt = millis();
  while (digitalRead(kPinButton) == LOW &&
         millis() - pressedAt < kLongPressMs) {
    delay(10);
    yield();
  }
  const bool longPress = digitalRead(kPinButton) == LOW;
  while (digitalRead(kPinButton) == LOW) {
    delay(10);
    yield();
  }

  if (longPress) {
    gPage = gPage == WEATHER_PAGE ? STOCK_PAGE : WEATHER_PAGE;
    Serial.println(gPage == WEATHER_PAGE ? F("FLASH long press: WEATHER page")
                                          : F("FLASH long press: STOCK page"));
  } else {
    Serial.println(F("FLASH short press: refresh current page"));
  }
  updateAndDisplay();
  gLastUpdate = millis();
}

}  // namespace

void setup() {
  digitalWrite(kPinCs, HIGH);
  pinMode(kPinCs, OUTPUT);
  digitalWrite(kPinDc, LOW);
  pinMode(kPinDc, OUTPUT);
  digitalWrite(kPinRst, HIGH);
  pinMode(kPinRst, OUTPUT);
  pinMode(kPinBusy, INPUT_PULLUP);
  pinMode(kPinButton, INPUT_PULLUP);
  SPI.begin();
  Serial.begin(115200);
  wifi_set_opmode_current(NULL_MODE);
  delay(500);
  Serial.println(F("SE0398NZ07A0 BYD + Shenzhen dashboard"));
  Serial.println(F("Short FLASH: refresh; long FLASH: switch page."));
  delay(3000);
  updateAndDisplay();
  gLastUpdate = millis();
}

void loop() {
  handleFlashButton();
  if (millis() - gLastUpdate >= kUpdateIntervalMs) {
    updateAndDisplay();
    gLastUpdate = millis();
  }
  delay(20);
  yield();
}
