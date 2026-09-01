#pragma once

#include <Arduino.h>

#include "epaper_display.h"

class Canvas1Bit {
 public:
  explicit Canvas1Bit(EpaperDisplay& display);

  void clear(bool black = false);
  void pixel(int16_t x, int16_t y, bool black = true);
  void line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
            bool black = true);
  void rect(int16_t x, int16_t y, int16_t width, int16_t height,
            bool black = true);
  void fillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                bool black = true);
  void text(int16_t x, int16_t y, const char* value, uint8_t scale = 1,
            bool black = true);
  int16_t textWidth(const char* value, uint8_t scale = 1) const;
  void progressBar(int16_t x, int16_t y, int16_t width, int16_t height,
                   uint8_t percent);
  void wifiIcon(int16_t x, int16_t y, bool connected, int8_t rssi);
  void batteryIcon(int16_t x, int16_t y, uint8_t percent);

 private:
  void character(int16_t x, int16_t y, char value, uint8_t scale,
                 bool black);

  EpaperDisplay& display_;
};

