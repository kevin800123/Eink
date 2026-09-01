#include "epaper_display.h"

#include <SPI.h>
#include <string.h>

#include "app_config.h"

namespace {

// Controller waveform data validated against Waveshare's product-specific
// ESP32-C6-ePaper-1.54 Arduino example (release 1.1.0).
constexpr uint8_t kFullRefreshWaveform[159] = {
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x80, 0x48, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x40, 0x48, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x01, 0x00, 0x08, 0x01,
    0x00, 0x02, 0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x22, 0x22, 0x22,
    0x22, 0x22, 0x22, 0x00, 0x00, 0x00, 0x22, 0x17, 0x41, 0x00, 0x32, 0x20,
};

SPISettings kEpaperSpiSettings(BoardConfig::EpaperSpiHz, MSBFIRST, SPI_MODE0);

}  // namespace

bool EpaperDisplay::begin() {
  pinMode(BoardConfig::EpaperChipSelect, OUTPUT);
  pinMode(BoardConfig::EpaperDataCommand, OUTPUT);
  pinMode(BoardConfig::EpaperReset, OUTPUT);
  pinMode(BoardConfig::EpaperBusy, INPUT);

  digitalWrite(BoardConfig::EpaperChipSelect, HIGH);
  digitalWrite(BoardConfig::EpaperDataCommand, HIGH);
  digitalWrite(BoardConfig::EpaperReset, HIGH);

  SPI.begin(BoardConfig::EpaperClock, BoardConfig::EpaperMiso,
            BoardConfig::EpaperMosi, BoardConfig::EpaperChipSelect);

  hardwareReset();
  if (!initializeFullRefresh()) {
    return false;
  }
  setError("ok");
  return true;
}

void EpaperDisplay::clear(bool black) {
  memset(buffer_, black ? 0x00 : 0xFF, sizeof(buffer_));
}

void EpaperDisplay::drawPixel(int16_t x, int16_t y, bool black) {
  if (x < 0 || y < 0 || x >= static_cast<int16_t>(Width) ||
      y >= static_cast<int16_t>(Height)) {
    return;
  }
  const size_t index = static_cast<size_t>(y) * (Width / 8) + (x >> 3);
  const uint8_t mask = 0x80U >> (x & 0x07);
  if (black) {
    buffer_[index] &= static_cast<uint8_t>(~mask);
  } else {
    buffer_[index] |= mask;
  }
}

bool EpaperDisplay::present() {
  setWindow(0, Height - 1, Width - 1, 0);
  setCursor(0, Height - 1);
  sendCommand(0x24);
  sendData(buffer_, sizeof(buffer_));
  updateFull();
  if (!waitUntilIdle()) {
    setError("e-paper BUSY timeout during refresh");
    return false;
  }
  setError("ok");
  return true;
}

void EpaperDisplay::sleep() {
  sendCommand(0x10);
  sendData(0x01);
  delay(10);
  digitalWrite(BoardConfig::EpaperReset, LOW);
}

const char* EpaperDisplay::lastError() const {
  return lastError_;
}

void EpaperDisplay::hardwareReset() {
  digitalWrite(BoardConfig::EpaperReset, HIGH);
  delay(50);
  digitalWrite(BoardConfig::EpaperReset, LOW);
  delay(20);
  digitalWrite(BoardConfig::EpaperReset, HIGH);
  delay(50);
}

bool EpaperDisplay::initializeFullRefresh() {
  if (!waitUntilIdle()) {
    setError("e-paper BUSY timeout before initialization");
    return false;
  }

  sendCommand(0x12);  // Software reset.
  if (!waitUntilIdle()) {
    setError("e-paper BUSY timeout after reset");
    return false;
  }

  sendCommand(0x01);  // Driver output: 200 gates.
  sendData(0xC7);
  sendData(0x00);
  sendData(0x01);

  sendCommand(0x11);  // X increments; Y decrements.
  sendData(0x01);
  setWindow(0, Height - 1, Width - 1, 0);

  sendCommand(0x3C);
  sendData(0x01);
  sendCommand(0x18);
  sendData(0x80);

  sendCommand(0x22);
  sendData(0xB1);
  sendCommand(0x20);
  setCursor(0, Height - 1);
  if (!waitUntilIdle()) {
    setError("e-paper BUSY timeout while loading temperature settings");
    return false;
  }

  loadFullRefreshWaveform();
  if (!waitUntilIdle()) {
    setError("e-paper BUSY timeout while loading waveform");
    return false;
  }
  return true;
}

bool EpaperDisplay::waitUntilIdle() {
  const uint32_t started = millis();
  while (digitalRead(BoardConfig::EpaperBusy) == HIGH) {
    if (millis() - started >= AI_DASH_BUSY_TIMEOUT_MS) {
      return false;
    }
    delay(5);
  }
  return true;
}

void EpaperDisplay::setWindow(uint16_t xStart, uint16_t yStart,
                              uint16_t xEnd, uint16_t yEnd) {
  sendCommand(0x44);
  sendData((xStart >> 3) & 0xFF);
  sendData((xEnd >> 3) & 0xFF);

  sendCommand(0x45);
  sendData(yStart & 0xFF);
  sendData((yStart >> 8) & 0xFF);
  sendData(yEnd & 0xFF);
  sendData((yEnd >> 8) & 0xFF);
}

void EpaperDisplay::setCursor(uint16_t x, uint16_t y) {
  sendCommand(0x4E);
  sendData(x & 0xFF);
  sendCommand(0x4F);
  sendData(y & 0xFF);
  sendData((y >> 8) & 0xFF);
}

void EpaperDisplay::loadFullRefreshWaveform() {
  sendCommand(0x32);
  sendData(kFullRefreshWaveform, 153);

  sendCommand(0x3F);
  sendData(kFullRefreshWaveform[153]);
  sendCommand(0x03);
  sendData(kFullRefreshWaveform[154]);
  sendCommand(0x04);
  sendData(kFullRefreshWaveform[155]);
  sendData(kFullRefreshWaveform[156]);
  sendData(kFullRefreshWaveform[157]);
  sendCommand(0x2C);
  sendData(kFullRefreshWaveform[158]);
}

void EpaperDisplay::updateFull() {
  sendCommand(0x22);
  sendData(0xC7);
  sendCommand(0x20);
}

void EpaperDisplay::sendCommand(uint8_t value) {
  SPI.beginTransaction(kEpaperSpiSettings);
  digitalWrite(BoardConfig::EpaperDataCommand, LOW);
  digitalWrite(BoardConfig::EpaperChipSelect, LOW);
  SPI.transfer(value);
  digitalWrite(BoardConfig::EpaperChipSelect, HIGH);
  SPI.endTransaction();
}

void EpaperDisplay::sendData(uint8_t value) {
  sendData(&value, 1);
}

void EpaperDisplay::sendData(const uint8_t* data, size_t length) {
  SPI.beginTransaction(kEpaperSpiSettings);
  digitalWrite(BoardConfig::EpaperDataCommand, HIGH);
  digitalWrite(BoardConfig::EpaperChipSelect, LOW);
  SPI.writeBytes(data, length);
  digitalWrite(BoardConfig::EpaperChipSelect, HIGH);
  SPI.endTransaction();
}

void EpaperDisplay::setError(const char* message) {
  lastError_ = message;
}

