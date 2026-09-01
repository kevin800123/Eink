# Hardware notes

## Target

- Board: Waveshare ESP32-C6-ePaper-1.54
- SKU: 34393
- Display: 1.54-inch, 200×200, black/white, SPI
- Arduino board: `ESP32C6 Dev Module`
- Verified core: `esp32 by Espressif Systems 3.3.11`

## Product-specific connections

These values come from the Waveshare C6 example at commit
`c4c47b6a8001f9daa50b38912393c158371e03be`.

| Function | ESP32-C6 GPIO |
|---|---:|
| EPD MOSI | 5 |
| EPD SCK | 6 |
| EPD CS | 7 |
| EPD BUSY | 10 |
| EPD RST | 11 |
| EPD DC | 15 |
| Shared SPI MISO | 4 |
| I2C SCL | 8 |
| I2C SDA | 18 |
| Battery ADC | GPIO 0 / ADC1 channel 0 |
| PWR button | 2 |
| BOOT button | 9 |

The TCA9554 is at I2C address `0x20`:

- P0: e-paper power, active high
- P1: audio power, active high; this project keeps it off
- P5: battery power hold, active high

The software writes output levels before changing TCA9554 direction bits. This
avoids a short low pulse on a controlled power rail.

## Refresh policy

- Version 0.1 uses full refresh only.
- The default interval is 15 minutes.
- The e-paper enters deep sleep and its power rail is disabled after refresh.
- The image remains visible without power; this is normal e-paper behavior.
- Full refresh visibly flashes. That is expected, not a crash.

Partial refresh is deliberately deferred. It needs ghosting limits, a periodic
full-refresh policy, and real-panel verification before it should be enabled.

## Source references

- [Waveshare product page](https://www.waveshare.com/esp32-c6-epaper-1.54.htm)
- [Waveshare documentation](https://docs.waveshare.com/ESP32-C6-ePaper-1.54)
- [Waveshare official example repository](https://github.com/waveshareteam/ESP32-C6-ePaper-1.54)

