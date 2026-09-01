#pragma once

#include <Arduino.h>

namespace BoardConfig {

constexpr uint16_t DisplayWidth = 200;
constexpr uint16_t DisplayHeight = 200;

constexpr uint8_t EpaperMiso = 4;
constexpr uint8_t EpaperMosi = 5;
constexpr uint8_t EpaperClock = 6;
constexpr uint8_t EpaperChipSelect = 7;
constexpr uint8_t EpaperBusy = 10;
constexpr uint8_t EpaperReset = 11;
constexpr uint8_t EpaperDataCommand = 15;

constexpr uint8_t I2cClock = 8;
constexpr uint8_t I2cData = 18;
constexpr uint8_t Tca9554Address = 0x20;
// Waveshare's TCA9554 driver runs the bus at 400kHz.
// esp_io_expander_tca9554.c:18.
constexpr uint32_t I2cClockHz = 400'000UL;

constexpr uint8_t TcaEpaperPowerMask = 1U << 0;
constexpr uint8_t TcaAudioPowerMask = 1U << 1;
constexpr uint8_t TcaLedMask = 1U << 4;
constexpr uint8_t TcaBatteryHoldMask = 1U << 5;

constexpr uint8_t BatteryAdc = 0;
constexpr uint8_t PowerButton = 2;
constexpr uint8_t BootButton = 9;

constexpr uint32_t EpaperSpiHz = 10'000'000UL;

}  // namespace BoardConfig

