# TCA9554 power sequence audit

Read-only audit of the board power sequence. This compares the old
Dashboard's `AI_Usage_Dashboard/board_power.cpp` against the Waveshare official
example, line by line.

**Status of the conclusion: the differences below are verified from source. The
root cause of the persistent I2C stuck state is still NOT verified.** Do not
present any single difference here as the proven trigger.

## Sources compared

Downloaded from the Waveshare official repository at the baseline commit
`c4c47b6a8001f9daa50b38912393c158371e03be`:

| File | Role |
|---|---|
| `02_Example/arduino_v3.3.0/07_BATT_PWR_Test/07_BATT_PWR_Test.ino` | power sequence |
| `.../07_BATT_PWR_Test/src/exio/esp_io_expander_tca9554.c` | TCA9554 driver |
| `.../07_BATT_PWR_Test/port_i2c.cpp` | I2C bus setup |
| `.../07_BATT_PWR_Test/epaper_config.h` | pin and EXIO map |

## Confirmed by this audit

The pin map in `docs/HARDWARE_NOTES.md` matches `epaper_config.h` exactly:
I2C SDA `GPIO18`, SCL `GPIO8`, EPD SPI `5/6/7/10/11/15`, MISO `4`,
PWR button `GPIO2`, BOOT button `GPIO9`, EXIO0 `EPD_PWR_PIN`,
EXIO1 `Audio_PWR_PIN`, EXIO5 `VBAT_PWR_PIN`, EXIO4 `LED_PIN`.

`epaper_config.h:56-58` also documents two more I2C devices that this project
never accounted for:

| Address | Device |
|---:|---|
| `0x51` | PCF85063 RTC |
| `0x70` | SHTC3 temperature / humidity |
| `0x38` | FT6336 touch (not populated on this SKU) |

This resolves an earlier open question. The factory residual image showing a
clock, `28°`, `68%` and `sdcard` is consistent with this board; it is not
evidence of a wrong product. It also means a healthy bus should ACK **three**
addresses, not one — a scan returning only `0x20` would itself be a finding.

## Official sequence

`07_BATT_PWR_Test.ino:54-58`, expanded through the driver:

```text
I2cMasterBus(SCL=8, SDA=18, I2C_NUM_0)
    enable_internal_pullup = true          port_i2c.cpp:27
    glitch_ignore_cnt      = 7             port_i2c.cpp:26
esp_io_expander_new_i2c_tca9554(0x20)
    scl_speed_hz = 400000                  esp_io_expander_tca9554.c:18,66
    reset():                               esp_io_expander_tca9554.c:142-147
        write DIRECTION 0x03 = 0xFF        (all pins input first)
        write OUTPUT    0x01 = 0xFF        (all latches high)
esp_io_expander_set_dir(P0|P1|P5, OUTPUT)
        write DIRECTION 0x03 = 0xDC
esp_io_expander_set_level(P0|P1|P5, 1)
        write OUTPUT    0x01 = 0xFF
```

Net effect: **direction is written before level**, and at the instant the pins
become outputs the latch already holds `0xFF`, so `P0`, `P1` and `P5` all drive
**high together**.

## Old Dashboard sequence

`board_power.cpp:14-45`:

```text
Wire.begin(SDA=18, SCL=8); Wire.setClock(400000)
read  OUTPUT    0x01 -> output_        (no official equivalent)
read  DIRECTION 0x03 -> direction_     (no official equivalent)
output_ |= P0; output_ |= P5; output_ &= ~P1     -> 0xFD
write OUTPUT    0x01 = 0xFD            (level first)
direction_ &= ~(P0|P1|P5)              -> 0xDC
write DIRECTION 0x03 = 0xDC            (direction second)
delay(20)
```

## Difference table

| # | Item | Official | Old Dashboard | Assessment |
|---:|---|---|---|---|
| 1 | `OUTPUT` value | `0xFF` | `0xFD` | **P1 (`Audio_PWR_PIN`) is driven low instead of high.** This is the only difference in the electrical state at the moment the pins become outputs. |
| 2 | Write order | direction, then level | level, then direction | Reversed. The old comment claims writing levels first avoids a rail glitch, but the official driver does the opposite and is the only sequence with on-device evidence on this board. |
| 3 | Reset step | `reset()` forces `DIR=0xFF` then `OUT=0xFF` before any configuration | none | The Dashboard has no reset step at all. |
| 4 | Register reads | never reads `0x01` / `0x03` from the chip; `read_output_reg` and `read_direction_reg` return **cached** values (`esp_io_expander_tca9554.c:115-140`) | reads both registers over I2C and does read-modify-write | The Dashboard inherits whatever the chip currently holds. **This matters on this board specifically:** a USB re-plug or an upload reset does not power-cycle the TCA9554, because the internal battery keeps it alive. Stale state therefore survives into the next boot and is folded into the computed value. |
| 5 | `DIRECTION` value | `0xDC` | `0xDC` | Identical. |
| 6 | I2C clock | `400000` | `400000` | Identical. |
| 7 | Internal pull-ups | explicit `enable_internal_pullup = true` | relies on the Arduino `Wire` default | Equivalent in effect; not explicit in our code. |
| 8 | `glitch_ignore_cnt` | `7` | not exposed by Arduino `Wire` | Cannot be matched with `Wire`. Impact unknown. |
| 9 | I2C timeout | `1000 ms`, plus `i2c_master_bus_wait_all_done` at `1000 ms` | Arduino `Wire` default | Ours is far shorter. Affects robustness, not the initial trigger. |
| 10 | PWR button release wait | `while (0 == gpio_get_level(PWR_BUTTON_PIN)) delay(100)` (`07_BATT_PWR_Test.ino:69-71`) | none | The official code refuses to continue while PWR is still held. Impact unverified. |
| 11 | Rail lifetime | sets `P0` high once and leaves it on | `AI_DASH_EPD_POWER_OFF_AFTER_REFRESH=1` toggles the EPD rail off after every refresh and on again 15 minutes later | The Dashboard power-cycles the display rail repeatedly; the official example never does. |
| 12 | `setEpaper()` re-entry | levels set once at init | `refreshDashboard()` calls `setEpaper(true)` again on every cycle, read-modify-writing `OUTPUT` from the cached `output_` | Extra writes with no official counterpart. |

## Ranked candidates for the stuck state

Ordered by how well each fits the evidence. **All remain unverified.**

1. **Difference 4 combined with the internal battery.** The chip is never power
   cycled by USB removal, so the Dashboard's read-modify-write can start from a
   non-power-on state left by the previous run. This is the only difference that
   naturally explains a fault that *persisted across reboots* and cleared only
   when the battery header was physically disconnected.
2. **Difference 1.** Driving `P1` low switches the audio rail off while the
   official sequence switches it on. Whether that produces a transient on a
   shared rail cannot be determined from source alone.
3. **Difference 11.** Repeated EPD rail power cycling is a plausible source of
   inrush transients, but the first failure happened before any second refresh,
   so it cannot explain the initial event.
4. **Difference 2.** Write order alone does not change the final register values,
   only the instant at which the pins start driving. Weakest of the four.

A latch-up in one of the three I2C devices would fit the observed symptom set
(both lines clamped low, surviving reset, cleared only by full power removal),
but nothing in the source proves which difference, if any, could induce it.
Confirming this needs instrumented hardware measurement, not more code reading.

## Update: the old sequence has now been reproduced

An accidental upload of the old binary (a stale build cache was flashed instead
of the current build) produced a same-day, same-board A/B comparison:

| Firmware | Sequence | Result |
|---|---|---|
| `tools/smoke/epd_smoke` | official | bus healthy, four devices ACK, refresh completes, survives repeated reboots |
| stale old binary | old | `EPD power-on failed` immediately, bus back in the persistent stuck state |

This raises "the old sequence is the trigger" from unverified to **reproduced
once under controlled conditions**. It still does not identify *which* of the
differences above is responsible, and it does not explain the mechanism that
makes the state survive a reset and require a battery disconnect. Do not write
it up as an established causal mechanism.

`board_power.cpp` now follows the official sequence and additionally refuses to
write anything when the bus is already held low.

## What the smoke firmware does about it

`tools/smoke/epd_smoke/` removes differences 1, 2, 3, 4, 11 and 12 by following
the official sequence exactly, and adds a pre-flight bus check that aborts
before writing anything if `SDA` or `SCL` is already held low.

Deliberate deviation from the original plan to keep
audio off, but the official example sets `P1` high, and matching the known-good
sequence bit for bit takes priority for the first on-device run. Audio can be
switched off later, as a single deliberate change, once a baseline exists.

`EXIO5` is written to `1` only, exactly as the official init does, and is never
cleared — clearing it is the official shutdown path.
