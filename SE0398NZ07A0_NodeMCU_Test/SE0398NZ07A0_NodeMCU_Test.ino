#include <Arduino.h>
#include <SPI.h>

extern "C" {
#include <user_interface.h>
}

namespace {

// NodeMCU 1.0 (ESP-12E/ESP-12F) pin assignment.
constexpr uint8_t kPinCs = D1;    // GPIO5  -> EPD CS
constexpr uint8_t kPinDc = D2;    // GPIO4  -> EPD DC
constexpr uint8_t kPinRst = D0;   // GPIO16 -> EPD RST
constexpr uint8_t kPinBusy = D6;  // GPIO12 <- EPD BUSY
// Hardware SPI uses D5/GPIO14 for SCK and D7/GPIO13 for MOSI.

constexpr uint16_t kPanelWidth = 768;
constexpr uint16_t kPanelHeight = 552;
constexpr uint16_t kHalfWidth = kPanelWidth / 2;
constexpr uint16_t kHalfHeight = kPanelHeight / 2;
constexpr size_t kRowBytes = kPanelWidth / 4;  // Four 2-bit pixels per byte.

constexpr uint32_t kSpiFrequencyHz = 1000000;
constexpr uint32_t kResetTimeoutMs = 10000;
constexpr uint32_t kBusyAssertTimeoutMs = 2000;
constexpr uint32_t kPowerTimeoutMs = 60000;
constexpr uint32_t kRefreshTimeoutMs = 180000;
constexpr uint32_t kAutoStartDelayMs = 3000;

// Change this to true only if the complete test image is exactly upside down.
constexpr bool kRotateImage180 = false;

enum Color : uint8_t {
  BLACK = 0,
  WHITE = 1,
  YELLOW = 2,
  RED = 3,
};

SPISettings gEpdSpiSettings(kSpiFrequencyHz, MSBFIRST, SPI_MODE0);
uint8_t gRowBuffer[kRowBytes];

void writeCommand(uint8_t command) {
  SPI.beginTransaction(gEpdSpiSettings);
  digitalWrite(kPinDc, LOW);
  digitalWrite(kPinCs, LOW);
  SPI.transfer(command);
  digitalWrite(kPinCs, HIGH);
  SPI.endTransaction();
}

void writeData(const uint8_t* data, size_t length) {
  SPI.beginTransaction(gEpdSpiSettings);
  digitalWrite(kPinDc, HIGH);
  for (size_t i = 0; i < length; ++i) {
    // Match the verified public A0 driver: release CS after every byte.
    digitalWrite(kPinCs, LOW);
    SPI.transfer(data[i]);
    digitalWrite(kPinCs, HIGH);
  }
  SPI.endTransaction();
}

void writeDataByte(uint8_t data) {
  writeData(&data, 1);
}

bool waitUntilReady(const __FlashStringHelper* phase, uint32_t timeoutMs) {
  const uint32_t start = millis();

  while (digitalRead(kPinBusy) == LOW) {
    if (millis() - start >= timeoutMs) {
      Serial.print(F("ERROR: BUSY timeout during "));
      Serial.println(phase);
      return false;
    }
    delay(10);
    yield();
  }

  return true;
}

bool waitForBusyCycle(const __FlashStringHelper* phase, uint32_t readyTimeoutMs) {
  const uint32_t assertStart = millis();
  while (digitalRead(kPinBusy) == HIGH) {
    if (millis() - assertStart >= kBusyAssertTimeoutMs) {
      Serial.print(F("ERROR: BUSY never went LOW during "));
      Serial.println(phase);
      Serial.println(F("Check BUSY, CS, CLK, MOSI and panel power."));
      return false;
    }
    delay(1);
    yield();
  }

  Serial.print(F("BUSY asserted LOW for "));
  Serial.println(phase);
  return waitUntilReady(phase, readyTimeoutMs);
}

bool hardwareReset() {
  digitalWrite(kPinCs, HIGH);
  digitalWrite(kPinRst, HIGH);
  delay(20);
  digitalWrite(kPinRst, LOW);
  delay(40);
  digitalWrite(kPinRst, HIGH);
  delay(50);

  if (!waitUntilReady(F("hardware reset"), kResetTimeoutMs)) {
    return false;
  }
  delay(30);
  return true;
}

bool initializePanel() {
  Serial.println(F("Resetting and initializing A0 panel..."));
  if (!hardwareReset()) {
    return false;
  }

  writeCommand(0x00);
  writeDataByte(0x0B);

  const uint8_t resolution[] = {0x03, 0x00, 0x02, 0x28};
  writeCommand(0x61);
  writeData(resolution, sizeof(resolution));
  return true;
}

uint16_t physicalRowForLogicalRow(uint16_t logicalRow) {
  if (logicalRow < kHalfHeight) {
    return logicalRow * 2;
  }
  return (kPanelHeight - 1) - 2 * (logicalRow - kHalfHeight);
}

void setWriteWindow(uint16_t firstRow, uint16_t lastRow) {
  const uint8_t window[] = {
      0x00,
      0x00,
      0x02,
      0xFF,
      static_cast<uint8_t>(firstRow >> 8),
      static_cast<uint8_t>(firstRow & 0xFF),
      static_cast<uint8_t>(lastRow >> 8),
      static_cast<uint8_t>(lastRow & 0xFF),
      0x01,
  };

  writeCommand(0x83);
  writeData(window, sizeof(window));
}

Color testPatternColor(uint16_t panelX, uint16_t panelY) {
  uint16_t x = panelX;
  uint16_t y = panelY;
  if (kRotateImage180) {
    x = (kPanelWidth - 1) - panelX;
    y = (kPanelHeight - 1) - panelY;
  }

  Color color;
  if (y < kHalfHeight) {
    color = x < kHalfWidth ? BLACK : WHITE;
  } else {
    color = x < kHalfWidth ? YELLOW : RED;
  }

  // Asymmetric markers make a 180-degree orientation error obvious.
  if (x >= 24 && x < 72 && y >= 24 && y < 72) {
    color = WHITE;
  }
  if (x >= kPanelWidth - 72 && x < kPanelWidth - 24 &&
      y >= kPanelHeight - 72 && y < kPanelHeight - 24) {
    color = BLACK;
  }

  return color;
}

void buildTestPatternRow(uint16_t logicalRow) {
  for (size_t byteIndex = 0; byteIndex < kRowBytes; ++byteIndex) {
    uint8_t packedPixels = 0;
    for (uint8_t pixelInByte = 0; pixelInByte < 4; ++pixelInByte) {
      const uint16_t x = byteIndex * 4 + pixelInByte;
      const uint8_t color = testPatternColor(x, logicalRow);
      packedPixels |= color << (6 - pixelInByte * 2);
    }
    gRowBuffer[byteIndex] = packedPixels;
  }
}

void writeTestPattern() {
  Serial.println(F("Writing 552 rows (192 bytes per row)..."));

  for (uint16_t logicalRow = 0; logicalRow < kPanelHeight; ++logicalRow) {
    buildTestPatternRow(logicalRow);
    const uint16_t physicalRow = physicalRowForLogicalRow(logicalRow);
    setWriteWindow(physicalRow, physicalRow);
    writeCommand(0x10);
    writeData(gRowBuffer, sizeof(gRowBuffer));

    if (logicalRow % 50 == 0) {
      Serial.print(F("  logical row "));
      Serial.print(logicalRow);
      Serial.print(F(" -> physical row "));
      Serial.println(physicalRow);
    }
    yield();
  }
}

bool refreshAndSleep() {
  Serial.println(F("Powering on EPD high-voltage section..."));
  setWriteWindow(0, kPanelHeight - 1);
  writeCommand(0x04);
  if (!waitForBusyCycle(F("power on"), kPowerTimeoutMs)) {
    return false;
  }

  Serial.println(F("Refreshing; four-color refresh may take tens of seconds..."));
  writeCommand(0x12);
  writeDataByte(0x00);
  if (!waitForBusyCycle(F("display refresh"), kRefreshTimeoutMs)) {
    return false;
  }

  Serial.println(F("Powering off EPD high-voltage section..."));
  writeCommand(0x02);
  writeDataByte(0x00);
  if (!waitForBusyCycle(F("power off"), kPowerTimeoutMs)) {
    return false;
  }

  writeCommand(0x07);
  writeDataByte(0xA5);
  return true;
}

void runDisplayTest() {
  Serial.println();
  Serial.println(F("=== SE0398NZ07A0 A0 TEST START ==="));
  Serial.print(F("BUSY before reset: "));
  Serial.println(digitalRead(kPinBusy) == HIGH ? F("HIGH/ready") : F("LOW/busy"));

  if (!initializePanel()) {
    Serial.println(F("Test stopped. Check 3.3V, GND, BUSY and RST."));
    return;
  }

  writeTestPattern();
  if (!refreshAndSleep()) {
    Serial.println(F("Test stopped. Check power stability and signal wiring."));
    return;
  }

  Serial.println(F("=== TEST COMPLETE ==="));
  Serial.println(F("Expected: BLACK|WHITE above YELLOW|RED."));
  Serial.println(F("A white marker is in BLACK; a black marker is in RED."));
}

}  // namespace

void setup() {
  digitalWrite(kPinCs, HIGH);
  pinMode(kPinCs, OUTPUT);
  digitalWrite(kPinDc, LOW);
  pinMode(kPinDc, OUTPUT);
  digitalWrite(kPinRst, HIGH);
  pinMode(kPinRst, OUTPUT);

  SPI.begin();  // ESP8266 hardware SPI: SCK=D5, MISO=D6, MOSI=D7.
  pinMode(kPinBusy, INPUT);  // D6 is not used for SPI reads in this project.

  Serial.begin(115200);
  wifi_set_opmode_current(NULL_MODE);  // Disable radio without writing flash.
  delay(500);
  Serial.println();
  Serial.println(F("SE0398NZ07A0 NodeMCU test firmware"));
  Serial.println(F("Automatic four-color test starts in 3 seconds."));
  Serial.println(F("No serial command is required."));
  delay(kAutoStartDelayMs);
  runDisplayTest();
}

void loop() {
  // Run once per boot only. Repeated full refreshes are unnecessary for this test.
  delay(1000);
  yield();
}
