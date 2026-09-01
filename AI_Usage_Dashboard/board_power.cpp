#include "board_power.h"

#include <Wire.h>

#include "board_config.h"

namespace {

constexpr uint8_t kOutputRegister = 0x01;
constexpr uint8_t kConfigurationRegister = 0x03;

}  // namespace

bool BoardPower::begin() {
  Wire.begin(BoardConfig::I2cData, BoardConfig::I2cClock);
  Wire.setClock(400000);

  if (!readRegister(kOutputRegister, output_) ||
      !readRegister(kConfigurationRegister, direction_)) {
    setError("TCA9554 not found at I2C address 0x20");
    return false;
  }

  // Prepare levels before changing directions to avoid a power rail glitch.
  output_ |= BoardConfig::TcaEpaperPowerMask;
  output_ |= BoardConfig::TcaBatteryHoldMask;
  output_ &= static_cast<uint8_t>(~BoardConfig::TcaAudioPowerMask);
  if (!writeRegister(kOutputRegister, output_)) {
    setError("unable to set TCA9554 power outputs");
    return false;
  }

  const uint8_t controlledPins = BoardConfig::TcaEpaperPowerMask |
                                 BoardConfig::TcaAudioPowerMask |
                                 BoardConfig::TcaBatteryHoldMask;
  direction_ &= static_cast<uint8_t>(~controlledPins);  // 0 = output
  if (!writeRegister(kConfigurationRegister, direction_)) {
    setError("unable to configure TCA9554 directions");
    return false;
  }

  delay(20);
  setError("ok");
  return true;
}

bool BoardPower::setEpaper(bool enabled) {
  return applyOutput(BoardConfig::TcaEpaperPowerMask, enabled);
}

bool BoardPower::shutdownBatteryPower() {
  if (!setEpaper(false)) {
    return false;
  }
  return applyOutput(BoardConfig::TcaBatteryHoldMask, false);
}

const char* BoardPower::lastError() const {
  return lastError_;
}

bool BoardPower::readRegister(uint8_t reg, uint8_t& value) {
  Wire.beginTransmission(BoardConfig::Tca9554Address);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(BoardConfig::Tca9554Address, static_cast<uint8_t>(1)) != 1) {
    return false;
  }
  value = Wire.read();
  return true;
}

bool BoardPower::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BoardConfig::Tca9554Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool BoardPower::applyOutput(uint8_t mask, bool enabled) {
  const uint8_t next = enabled ? (output_ | mask)
                               : (output_ & static_cast<uint8_t>(~mask));
  if (!writeRegister(kOutputRegister, next)) {
    setError("unable to update TCA9554 output");
    return false;
  }
  output_ = next;
  setError("ok");
  return true;
}

void BoardPower::setError(const char* message) {
  lastError_ = message;
}

