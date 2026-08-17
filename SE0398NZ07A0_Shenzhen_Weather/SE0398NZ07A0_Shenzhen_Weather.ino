#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>

#include "secrets.h"
#include "weather_font.h"

extern "C" {
#include <user_interface.h>
}

namespace {

constexpr uint8_t kPinCs = D1;
constexpr uint8_t kPinDc = D2;
constexpr uint8_t kPinRst = D0;
constexpr uint8_t kPinBusy = D6;
constexpr uint8_t kPinFlash = D3;
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

enum Color : uint8_t { BLACK = 0, WHITE = 1, YELLOW = 2, RED = 3 };

struct WeatherData {
  float temperature = 0;
  float apparent = 0;
  int humidity = 0;
  int currentCode = 3;
  float high[3] = {0, 0, 0};
  float low[3] = {0, 0, 0};
  int rain[3] = {0, 0, 0};
  int codes[3] = {3, 3, 3};
  char dates[3][11] = {{0}};
  char sunrise[3][6] = {{0}};
  char sunset[3][6] = {{0}};
  bool valid = false;
  char updated[20] = "--:--";
};

SPISettings gSpi(kSpiFrequencyHz, MSBFIRST, SPI_MODE0);
WeatherData gWeather;
uint8_t gRowBuffer[kRowBytes];
uint32_t gLastUpdate = 0;

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
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    return false;
  }

  std::unique_ptr<BearSSL::WiFiClientSecure> client(
      new BearSSL::WiFiClientSecure);
  client->setInsecure();
  client->setBufferSizes(512, 512);
  HTTPClient http;
  const char* url =
      "https://api.open-meteo.com/v1/forecast?latitude=22.5431&longitude=114.0579&timezone=Asia%2FShanghai&forecast_days=3&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code,sunrise,sunset";
  bool ok = false;
  if (http.begin(*client, url)) {
    http.setTimeout(20000);
    const int status = http.GET();
    if (status == HTTP_CODE_OK) {
      const String body = http.getString();
      WeatherData next;
      next.temperature = jsonNumber(body, "temperature_2m");
      next.apparent = jsonNumber(body, "apparent_temperature");
      next.humidity = static_cast<int>(jsonNumber(body, "relative_humidity_2m"));
      next.currentCode = static_cast<int>(jsonNumber(body, "weather_code"));
      for (int i = 0; i < 3; ++i) {
        next.high[i] = jsonArrayNumber(body, "temperature_2m_max", i);
        next.low[i] = jsonArrayNumber(body, "temperature_2m_min", i);
        next.rain[i] = static_cast<int>(jsonArrayNumber(body, "precipitation_probability_max", i));
        next.codes[i] = static_cast<int>(jsonArrayNumber(body, "weather_code", i));
        jsonArrayString(body, "time", i, next.dates[i], sizeof(next.dates[i]));
        jsonArrayString(body, "sunrise", i, next.sunrise[i], sizeof(next.sunrise[i]));
        jsonArrayString(body, "sunset", i, next.sunset[i], sizeof(next.sunset[i]));
      }
      next.valid = !isnan(next.temperature) && next.dates[0][0] != 0;
      if (next.valid) {
        gWeather = next;
        snprintf(gWeather.updated, sizeof(gWeather.updated), "%02d:%02d",
                 (millis() / 3600000) % 24, (millis() / 60000) % 60);
        ok = true;
      }
    } else {
      Serial.printf("HTTP status: %d\n", status);
    }
    http.end();
  }
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  pinMode(kPinBusy, INPUT_PULLUP);
  delay(200);
  Serial.println(ok ? F("Weather updated.") : F("Weather parse failed."));
  return ok;
}

bool glyphPixel(uint32_t codepoint, uint8_t x, uint8_t y) {
  if (x >= 16 || y >= 16) return false;
  for (uint8_t i = 0; i < kWeatherFontCount; ++i) {
    const uint32_t stored = pgm_read_dword(&kWeatherFont[i].codepoint);
    if (stored == codepoint) {
      const uint8_t bits = pgm_read_byte(&kWeatherFont[i].bitmap[y * 2 + x / 8]);
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
  if (c >= '0' && c <= '9') return digits[c - '0'];
  if (c == '-') return minus;
  if (c == '.') return dot;
  if (c == ':') return colon;
  if (c == '/') return slash;
  if (c == 'C') return celsius;
  return nullptr;
}

void setPixel(uint16_t x, Color color) {
  if (x >= kPanelWidth) return;
  const uint16_t byte = x / 4;
  const uint8_t shift = 6 - (x & 3) * 2;
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
      if (!glyphPixel(codepoint, gx, gy)) continue;
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
        if (!(glyph[gy] & (1 << (4 - gx)))) continue;
        for (uint8_t sx = 0; sx < scale; ++sx) setPixel(cursor + gx * scale + sx, color);
      }
    }
    cursor += 6 * scale;
  }
}

bool circle(uint16_t x, uint16_t y, int16_t cx, int16_t cy, int16_t radius) {
  const int16_t dx = static_cast<int16_t>(x) - cx;
  const int16_t dy = static_cast<int16_t>(y) - cy;
  return dx * dx + dy * dy <= radius * radius;
}

void drawIconRow(uint16_t row, uint16_t left, uint16_t top, int code) {
  const bool sunny = code == 0;
  const bool rainy = code >= 51 && code <= 82;
  const bool storm = code >= 95;
  const int16_t cx = left + 48;
  const int16_t cy = top + 38;
  if (sunny || code <= 2) {
    for (uint16_t x = left; x < left + 96; ++x) {
      if (circle(x, row, cx, cy, 22)) setPixel(x, YELLOW);
    }
  }
  if (!sunny) {
    if (row >= top + 30 && row < top + 60) {
      for (uint16_t x = left + 8; x < left + 88; ++x) {
        if (circle(x, row, left + 35, top + 48, 22) ||
            circle(x, row, left + 60, top + 42, 19)) setPixel(x, BLACK);
      }
    }
    if (rainy || storm) {
      for (uint16_t x = left + 22; x < left + 82; x += 22) {
        if (row >= top + 62 && row < top + 78) setPixel(x, RED);
      }
    }
  }
}

void drawWeatherRow(uint16_t row) {
  fillRow(WHITE);
  if (row < 6) {
    for (uint16_t x = 0; x < kPanelWidth; ++x) setPixel(x, RED);
  }
  drawCjkRow(row, 22, 18, 2, "深圳天气", BLACK);
  drawAsciiRow(row, 600, 23, 2, gWeather.dates[0], BLACK);
  drawRectRow(row, 76, 292, 16, 752, BLACK);
  drawIconRow(row, 42, 112, gWeather.currentCode);
  char current[12];
  snprintf(current, sizeof(current), "%.0fC", gWeather.temperature);
  drawAsciiRow(row, 190, 110, 4, current, BLACK);
  drawCjkRow(row, 194, 214, 1, weatherText(gWeather.currentCode), RED);
  char details[24];
  snprintf(details, sizeof(details), "%.0fC", gWeather.apparent);
  drawCjkRow(row, 194, 248, 1, "体感", BLACK);
  drawAsciiRow(row, 268, 250, 2, details, BLACK);
  char humidity[12];
  snprintf(humidity, sizeof(humidity), "%d%%", gWeather.humidity);
  drawCjkRow(row, 410, 248, 1, "湿度", BLACK);
  drawAsciiRow(row, 482, 250, 2, humidity, BLACK);

  for (uint8_t day = 0; day < 3; ++day) {
    const uint16_t left = 16 + day * 248;
    drawRectRow(row, 318, 540, left, left + 232, BLACK);
    drawCjkRow(row, left + 16, 334, 1,
               day == 0 ? "今日" : (day == 1 ? "明日" : "后日"), BLACK);
    drawAsciiRow(row, left + 100, 338, 2, gWeather.dates[day] + 5, BLACK);
    drawIconRow(row, left + 64, 372, gWeather.codes[day]);
    char range[24];
    snprintf(range, sizeof(range), "%.0f-%.0fC", gWeather.high[day], gWeather.low[day]);
    drawAsciiRow(row, left + 18, 468, 2, range, RED);
    char rain[12];
    snprintf(rain, sizeof(rain), "%d%%", gWeather.rain[day]);
    drawCjkRow(row, left + 18, 500, 1, "降水", BLACK);
    drawAsciiRow(row, left + 88, 502, 2, rain, BLACK);
  }
  drawCjkRow(row, 530, 294, 1, "更新", BLACK);
  drawAsciiRow(row, 600, 296, 2, gWeather.updated, BLACK);
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

bool updateAndDisplay() {
  fetchWeather();
  if (!gWeather.valid) {
    Serial.println(F("No weather data yet; display skipped."));
    return false;
  }
  if (!initializePanel()) return false;
  writeWeatherImage();
  const bool refreshed = refreshPanel();
  Serial.println(refreshed ? F("Weather display complete.")
                           : F("Weather display failed."));
  return refreshed;
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
  pinMode(kPinFlash, INPUT_PULLUP);
  SPI.begin();
  Serial.begin(115200);
  wifi_set_opmode_current(NULL_MODE);
  delay(500);
  Serial.println(F("SE0398NZ07A0 Shenzhen weather dashboard"));
  delay(3000);
  updateAndDisplay();
  gLastUpdate = millis();
}

void loop() {
  static bool lastButton = HIGH;
  const bool button = digitalRead(kPinFlash);
  if (lastButton == HIGH && button == LOW) {
    delay(kButtonDebounceMs);
    if (digitalRead(kPinFlash) == LOW) {
      Serial.println(F("FLASH: force weather update"));
      updateAndDisplay();
      gLastUpdate = millis();
      while (digitalRead(kPinFlash) == LOW) delay(5);
    }
  }
  lastButton = digitalRead(kPinFlash);
  if (millis() - gLastUpdate >= kUpdateIntervalMs) {
    updateAndDisplay();
    gLastUpdate = millis();
  }
  delay(20);
  yield();
}
