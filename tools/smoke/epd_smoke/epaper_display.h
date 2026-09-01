#pragma once

#include <Arduino.h>

#include "board_config.h"

class EpaperDisplay {
 public:
  static constexpr uint16_t Width = BoardConfig::DisplayWidth;
  static constexpr uint16_t Height = BoardConfig::DisplayHeight;
  static constexpr size_t BufferSize = Width * Height / 8;

  bool begin();
  void clear(bool black = false);
  void drawPixel(int16_t x, int16_t y, bool black);
  bool present();
  void sleep();
  const char* lastError() const;

 private:
  void hardwareReset();
  bool initializeFullRefresh();
  bool waitUntilIdle();
  void setWindow(uint16_t xStart, uint16_t yStart,
                 uint16_t xEnd, uint16_t yEnd);
  void setCursor(uint16_t x, uint16_t y);
  void loadFullRefreshWaveform();
  void updateFull();
  void sendCommand(uint8_t value);
  void sendData(uint8_t value);
  void sendData(const uint8_t* data, size_t length);
  void setError(const char* message);

  uint8_t buffer_[BufferSize]{};
  const char* lastError_ = "not initialized";
};

