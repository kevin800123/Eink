#include "canvas_1bit.h"

#include <string.h>

namespace {

struct Glyph {
  char value;
  uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},
    {'A', {14, 17, 17, 31, 17, 17, 17}},
    {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {15, 16, 16, 16, 16, 16, 15}},
    {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}},
    {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {15, 16, 16, 23, 17, 17, 15}},
    {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},
    {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}},
    {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}},
    {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}},
    {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}},
    {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},
    {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}},
    {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}},
    {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},
    {'Z', {31, 1, 2, 4, 8, 16, 31}},
    {'0', {14, 17, 19, 21, 25, 17, 14}},
    {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},
    {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},
    {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}},
    {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}},
    {'9', {14, 17, 17, 15, 1, 1, 14}},
    {':', {0, 4, 4, 0, 4, 4, 0}},
    {'%', {25, 26, 4, 8, 11, 19, 0}},
    {'.', {0, 0, 0, 0, 0, 4, 4}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},
    {'/', {1, 2, 4, 8, 16, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},
    {'_', {0, 0, 0, 0, 0, 0, 31}},
    {'?', {14, 17, 1, 2, 4, 0, 4}},
};

const uint8_t* glyphRows(char value) {
  if (value >= 'a' && value <= 'z') {
    value = static_cast<char>(value - 'a' + 'A');
  }
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.value == value) {
      return glyph.rows;
    }
  }
  return kGlyphs[sizeof(kGlyphs) / sizeof(kGlyphs[0]) - 1].rows;
}

}  // namespace

Canvas1Bit::Canvas1Bit(EpaperDisplay& display) : display_(display) {}

void Canvas1Bit::clear(bool black) {
  display_.clear(black);
}

void Canvas1Bit::pixel(int16_t x, int16_t y, bool black) {
  display_.drawPixel(x, y, black);
}

void Canvas1Bit::line(int16_t x0, int16_t y0, int16_t x1, int16_t y1,
                      bool black) {
  const int16_t dx = abs(x1 - x0);
  const int16_t sx = x0 < x1 ? 1 : -1;
  const int16_t dy = -abs(y1 - y0);
  const int16_t sy = y0 < y1 ? 1 : -1;
  int16_t error = dx + dy;

  while (true) {
    pixel(x0, y0, black);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int16_t doubled = 2 * error;
    if (doubled >= dy) {
      error += dy;
      x0 += sx;
    }
    if (doubled <= dx) {
      error += dx;
      y0 += sy;
    }
  }
}

void Canvas1Bit::rect(int16_t x, int16_t y, int16_t width, int16_t height,
                      bool black) {
  if (width <= 0 || height <= 0) {
    return;
  }
  line(x, y, x + width - 1, y, black);
  line(x, y + height - 1, x + width - 1, y + height - 1, black);
  line(x, y, x, y + height - 1, black);
  line(x + width - 1, y, x + width - 1, y + height - 1, black);
}

void Canvas1Bit::fillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                          bool black) {
  for (int16_t row = 0; row < height; ++row) {
    line(x, y + row, x + width - 1, y + row, black);
  }
}

void Canvas1Bit::text(int16_t x, int16_t y, const char* value, uint8_t scale,
                      bool black) {
  if (value == nullptr || scale == 0) {
    return;
  }
  while (*value != '\0') {
    character(x, y, *value, scale, black);
    x += 6 * scale;
    ++value;
  }
}

int16_t Canvas1Bit::textWidth(const char* value, uint8_t scale) const {
  if (value == nullptr || *value == '\0' || scale == 0) {
    return 0;
  }
  return static_cast<int16_t>(strlen(value) * 6 * scale - scale);
}

void Canvas1Bit::progressBar(int16_t x, int16_t y, int16_t width,
                             int16_t height, uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  rect(x, y, width, height, true);
  const int16_t innerWidth = width - 4;
  const int16_t filled = static_cast<int16_t>(innerWidth * percent / 100);
  if (filled > 0) {
    fillRect(x + 2, y + 2, filled, height - 4, true);
  }
}

void Canvas1Bit::wifiIcon(int16_t x, int16_t y, bool connected, int8_t rssi) {
  if (!connected) {
    line(x, y, x + 12, y + 12, true);
    line(x + 12, y, x, y + 12, true);
    return;
  }
  uint8_t bars = 1;
  if (rssi >= -75) {
    bars = 2;
  }
  if (rssi >= -60) {
    bars = 3;
  }
  for (uint8_t index = 0; index < 3; ++index) {
    const int16_t barHeight = 4 + index * 4;
    rect(x + index * 5, y + 12 - barHeight, 3, barHeight, true);
    if (index < bars) {
      fillRect(x + index * 5, y + 12 - barHeight, 3, barHeight, true);
    }
  }
}

void Canvas1Bit::batteryIcon(int16_t x, int16_t y, uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }
  rect(x, y, 24, 12, true);
  fillRect(x + 24, y + 3, 2, 6, true);
  const int16_t fill = static_cast<int16_t>(18 * percent / 100);
  if (fill > 0) {
    fillRect(x + 3, y + 3, fill, 6, true);
  }
}

void Canvas1Bit::character(int16_t x, int16_t y, char value, uint8_t scale,
                           bool black) {
  const uint8_t* rows = glyphRows(value);
  for (uint8_t row = 0; row < 7; ++row) {
    for (uint8_t column = 0; column < 5; ++column) {
      if ((rows[row] & (1U << (4 - column))) != 0) {
        fillRect(x + column * scale, y + row * scale, scale, scale, black);
      }
    }
  }
}
