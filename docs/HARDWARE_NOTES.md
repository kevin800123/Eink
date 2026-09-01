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

## Verified recovery evidence

- SKU 34393 has an internal lithium battery connected through the board's
  MX1.25 2-pin battery header. Disconnecting USB is not a full power cycle.
- After the old Dashboard's first power sequence, both SDA GPIO18 and SCL GPIO8
  were externally held low from early boot; no device ACKed at 100kHz or 400kHz.
- The Waveshare factory firmware also failed while that state persisted.
- Physically disconnecting the battery header, waiting, and reconnecting it
  restored the bus. The factory firmware then refreshed the display and passed
  a PWR `OFF -> ON` cycle.
- This proves the hardware worked after the recovery. It does not yet prove the
  Dashboard power sequence is safe or identify the original trigger.

Keep EXIO5 (`BAT_Control`) out of ordinary display-power changes. Do not call
USB removal a full power cycle, and do not attempt blind 9-clock bus recovery
when SCL itself is held low.

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

