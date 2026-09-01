#pragma once

#include <Arduino.h>

class BoardPower {
 public:
  bool begin();
  bool setEpaper(bool enabled);
  bool shutdownBatteryPower();
  const char* lastError() const;

 private:
  bool readRegister(uint8_t reg, uint8_t& value);
  bool writeRegister(uint8_t reg, uint8_t value);
  bool applyOutput(uint8_t mask, bool enabled);
  void setError(const char* message);

  uint8_t output_ = 0xFF;
  uint8_t direction_ = 0xFF;
  const char* lastError_ = "not initialized";
};

