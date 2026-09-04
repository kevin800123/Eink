#include "board_power.h"

#include <Wire.h>

#include "board_config.h"

namespace {

// Register map, matching Waveshare's esp_io_expander_tca9554.c:23-25.
constexpr uint8_t kOutputRegister = 0x01;
constexpr uint8_t kConfigurationRegister = 0x03;

// Values the official driver's reset() forces before anything is configured.
// esp_io_expander_tca9554.c:28-29 and :142-147.
constexpr uint8_t kDirectionDefault = 0xFF;
constexpr uint8_t kOutputDefault = 0xFF;

// EXIO0 EPD power, EXIO1 audio power, EXIO5 battery hold. The official
// 07_BATT_PWR_Test.ino:57-58 drives exactly these three as outputs, all high.
constexpr uint8_t kControlledPins = BoardConfig::TcaEpaperPowerMask |
                                    BoardConfig::TcaAudioPowerMask |
                                    BoardConfig::TcaBatteryHoldMask;

}  // namespace

bool BoardPower::begin() {
  // Pre-flight. A line already held low means an earlier session left the bus
  // stuck. Writing into that state is exactly what must not happen again, and a
  // blind 9-clock recovery cannot help when SCL itself is the line being held.
  // Fail loudly instead; only disconnecting the battery header clears it.
  pinMode(BoardConfig::I2cData, INPUT_PULLUP);
  pinMode(BoardConfig::I2cClock, INPUT_PULLUP);
  delay(5);
  if (digitalRead(BoardConfig::I2cData) == LOW ||
      digitalRead(BoardConfig::I2cClock) == LOW) {
    setError("I2C bus held low before init; disconnect the battery header");
    return false;
  }

  Wire.begin(BoardConfig::I2cData, BoardConfig::I2cClock);
  Wire.setClock(BoardConfig::I2cClockHz);

  // Follow the official order: reset to a known state, then set directions,
  // then set levels. The official driver never reads these registers back; it
  // keeps a cache instead (esp_io_expander_tca9554.c:115-140). That matters on
  // this board because the internal battery keeps the TCA9554 powered across a
  // USB re-plug or an upload reset, so a read-modify-write would inherit
  // whatever the previous firmware left behind.
  if (!writeRegister(kConfigurationRegister, kDirectionDefault) ||
      !writeRegister(kOutputRegister, kOutputDefault)) {
    setError("TCA9554 not responding at I2C address 0x20");
    return false;
  }

  direction_ = static_cast<uint8_t>(kDirectionDefault & ~kControlledPins);
  if (!writeRegister(kConfigurationRegister, direction_)) {
    setError("unable to configure TCA9554 directions");
    return false;
  }

  output_ = kOutputDefault;
  if (!writeRegister(kOutputRegister, output_)) {
    setError("unable to set TCA9554 power outputs");
    return false;
  }

  delay(20);
  setError("ok");
  return true;
}

bool BoardPower::resume() {
  // Called after a deep-sleep timer wake. The TCA9554 kept the battery rail
  // latched (EXIO5 high) and the EPD rail on throughout sleep, because it is
  // powered by the very rail it holds. So DO NOT run begin()'s reset: its
  // intermediate "all inputs" (0xFF) step stops actively driving EXIO5 and can
  // drop the rail on battery-only power. Instead re-establish the I2C master
  // and write the known-good direction and level directly. Writing the
  // direction as 0xDC drives EXIO5 from the retained output latch (already 1),
  // so the hold never glitches.
  Wire.begin(BoardConfig::I2cData, BoardConfig::I2cClock);
  Wire.setClock(BoardConfig::I2cClockHz);

  direction_ = static_cast<uint8_t>(kDirectionDefault & ~kControlledPins);
  output_ = kOutputDefault;
  if (!writeRegister(kConfigurationRegister, direction_) ||
      !writeRegister(kOutputRegister, output_)) {
    setError("TCA9554 not responding after wake");
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

bool BoardPower::writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(BoardConfig::Tca9554Address);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool BoardPower::applyOutput(uint8_t mask, bool enabled) {
  const uint8_t next = enabled ? (output_ | mask)
                               : (output_ & static_cast<uint8_t>(~mask));
  if (next == output_) {
    // Already in the requested state. The official example sets each rail once
    // and never rewrites it, so skip the redundant transaction.
    setError("ok");
    return true;
  }
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
