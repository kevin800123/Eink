// One-shot EPD smoke test for Waveshare ESP32-C6-ePaper-1.54 (SKU 34393).
//
// WHY THIS EXISTS
// ---------------
// The old Dashboard left the I2C bus in a persistent stuck state (SDA GPIO18 and
// SCL GPIO8 both externally held low, zero devices ACKing) that only cleared
// after physically disconnecting the internal battery header. The root cause is
// NOT verified. This sketch is the minimum path needed to re-establish a known
// good on-device baseline, and it deliberately mirrors the Waveshare official
// example instead of the old Dashboard sequence.
//
// REFERENCE (downloaded at commit c4c47b6a8001f9daa50b38912393c158371e03be)
//   02_Example/arduino_v3.3.0/07_BATT_PWR_Test/07_BATT_PWR_Test.ino     : 54-58
//   02_Example/arduino_v3.3.0/07_BATT_PWR_Test/src/exio/
//       esp_io_expander_tca9554.c                                       : 17-29, 142-147
//   02_Example/arduino_v3.3.0/07_BATT_PWR_Test/epaper_config.h          : 32-44
//
// SAFETY RULES ENFORCED HERE
//   1. Pre-flight bus check. If SDA or SCL is held low the sketch ABORTS and
//      never writes anything. It does NOT attempt a blind 9-clock recovery,
//      which cannot work while SCL itself is held low.
//   2. Stops on the FIRST I2C error. No retry loop, no periodic re-attempt.
//   3. All hardware actions run exactly once, in setup(). loop() only reprints
//      the stored report, because this board has no RST button.
//   4. EXIO5 (VBAT_PWR / battery hold) is only ever written to 1, the same value
//      the official example uses at init. It is never cleared here; clearing it
//      is the official shutdown path and must not happen during a display test.
//
// NOT VERIFIED BY THIS SKETCH: the original stuck-state trigger, long-term
// refresh stability, and behaviour across repeated power cycles.

#include <Arduino.h>
#include <Wire.h>

#include "board_config.h"
#include "epaper_display.h"

namespace {

// TCA9554 register map — matches esp_io_expander_tca9554.c:23-25.
constexpr uint8_t kInputRegister = 0x00;
constexpr uint8_t kOutputRegister = 0x01;
constexpr uint8_t kDirectionRegister = 0x03;

// Power-up defaults the official driver forces during reset().
// esp_io_expander_tca9554.c:28-29 and :142-147.
constexpr uint8_t kDirectionDefault = 0xFF;
constexpr uint8_t kOutputDefault = 0xFF;

// EXIO0 EPD_PWR, EXIO1 Audio_PWR, EXIO5 VBAT_PWR — epaper_config.h:32-34.
// 07_BATT_PWR_Test.ino:57-58 drives all three as outputs, all three high.
constexpr uint8_t kControlledPins = (1U << 0) | (1U << 1) | (1U << 5);  // 0x23

// The official example leaves the EPD rail powered after drawing. Keep the same
// behaviour for the first smoke run so this differs from the known-good
// sequence in as few ways as possible. Revisit only after smoke passes.
constexpr bool kPowerOffEpdAfterRefresh = false;

// Official bus speed — esp_io_expander_tca9554.c:18.
constexpr uint32_t kI2cClockHz = 400000;

EpaperDisplay display;

// The board has no RST button, so the report is buffered and reprinted forever
// instead of being lost if the Serial Monitor attaches late.
char report[2048];
size_t reportLen = 0;

void logf(const char* fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  Serial.print(line);
  const size_t n = strlen(line);
  if (reportLen + n + 1 < sizeof(report)) {
    memcpy(report + reportLen, line, n);
    reportLen += n;
    report[reportLen] = '\0';
  }
}

const char* txName(uint8_t code) {
  switch (code) {
    case 0: return "OK";
    case 1: return "DATA_TOO_LONG";
    case 2: return "NACK_ADDR";
    case 3: return "NACK_DATA";
    case 4: return "OTHER(bus-level failure)";
    case 5: return "TIMEOUT(bus stuck low)";
    default: return "UNKNOWN";
  }
}

bool writeRegister(uint8_t reg, uint8_t value, const char* step) {
  Wire.beginTransmission(BoardConfig::Tca9554Address);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t code = Wire.endTransmission();
  logf("  %-22s reg 0x%02X = 0x%02X -> %s\n", step, reg, value, txName(code));
  return code == 0;
}

// Read-only. Used purely as evidence; never gates the power sequence.
bool readRegister(uint8_t reg, uint8_t& value) {
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

void fillRect(int16_t x, int16_t y, int16_t w, int16_t h) {
  for (int16_t row = y; row < y + h; ++row) {
    for (int16_t col = x; col < x + w; ++col) {
      display.drawPixel(col, row, true);
    }
  }
}

// Deliberately asymmetric in both axes so a photo proves the real orientation:
// a solid square marks the intended TOP-LEFT, and the letter F cannot be
// confused with its own mirror or its 180-degree rotation.
void drawOrientationPattern() {
  display.clear(false);

  fillRect(0, 0, EpaperDisplay::Width, 3);                              // top edge
  fillRect(0, EpaperDisplay::Height - 3, EpaperDisplay::Width, 3);      // bottom edge
  fillRect(0, 0, 3, EpaperDisplay::Height);                             // left edge
  fillRect(EpaperDisplay::Width - 3, 0, 3, EpaperDisplay::Height);      // right edge

  fillRect(12, 12, 30, 30);      // solid block = intended top-left corner

  fillRect(70, 60, 16, 95);      // F stem
  fillRect(70, 60, 70, 16);      // F top arm
  fillRect(70, 98, 52, 14);      // F middle arm
}

void haltForever(const char* verdict) {
  logf("\nRESULT: %s\n", verdict);
  logf("(one-shot complete; no further I2C or SPI activity will occur)\n");
  while (true) {
    Serial.println();
    Serial.println("========== EPD SMOKE TEST REPORT ==========");
    Serial.print(report);
    Serial.println("=========== end of report ===========");
    delay(3000);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(300);

  logf("\n===== EPD ONE-SHOT SMOKE TEST =====\n");
  logf("Sequence source: Waveshare 07_BATT_PWR_Test @ c4c47b6\n");
  logf("SDA=GPIO%u SCL=GPIO%u TCA9554=0x%02X\n",
       BoardConfig::I2cData, BoardConfig::I2cClock,
       BoardConfig::Tca9554Address);

  // --- Step 1: pre-flight bus check. Abort before writing anything. ---
  pinMode(BoardConfig::I2cData, INPUT_PULLUP);
  pinMode(BoardConfig::I2cClock, INPUT_PULLUP);
  delay(5);
  const int sdaLevel = digitalRead(BoardConfig::I2cData);
  const int sclLevel = digitalRead(BoardConfig::I2cClock);
  logf("[1] pre-flight idle levels: SDA=%d SCL=%d\n", sdaLevel, sclLevel);
  if (sdaLevel == 0 || sclLevel == 0) {
    logf("    ABORT: bus is held low before any transaction.\n");
    logf("    Do NOT power-cycle by USB alone; this board has an internal\n");
    logf("    battery. Recovery requires disconnecting the battery header.\n");
    haltForever("ABORTED - I2C bus held low, nothing was written");
  }
  logf("    both lines high; bus looks idle\n");

  Wire.begin(BoardConfig::I2cData, BoardConfig::I2cClock);
  Wire.setClock(kI2cClockHz);

  // --- Step 2: read-only inventory. Evidence only. ---
  logf("[2] address probe:");
  uint8_t deviceCount = 0;
  for (uint8_t addr = 0x08; addr <= 0x77; ++addr) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      logf(" 0x%02X", addr);
      deviceCount++;
    }
  }
  if (deviceCount == 0) {
    logf(" <none>");
  }
  logf("  (total=%u)\n", deviceCount);
  logf("    expected on this board: 0x20 TCA9554, 0x51 PCF85063, 0x70 SHTC3\n");

  if (deviceCount == 0) {
    haltForever("ABORTED - no I2C device responded");
  }

  // --- Step 3: TCA9554 init in the official order. ---
  // esp_io_expander_tca9554.c reset() writes DIRECTION first, then OUTPUT.
  // 07_BATT_PWR_Test.ino then sets direction, then level. The old Dashboard did
  // the opposite (output first, then direction) and cleared EXIO1; both of those
  // differences are removed here.
  logf("[3] TCA9554 init (official order: reset -> dir -> level)\n");

  if (!writeRegister(kDirectionRegister, kDirectionDefault, "reset:direction")) {
    haltForever("FAILED - TCA9554 reset direction write");
  }
  if (!writeRegister(kOutputRegister, kOutputDefault, "reset:output")) {
    haltForever("FAILED - TCA9554 reset output write");
  }

  const uint8_t direction =
      static_cast<uint8_t>(kDirectionDefault & ~kControlledPins);  // 0xDC
  if (!writeRegister(kDirectionRegister, direction, "set_dir P0|P1|P5")) {
    haltForever("FAILED - TCA9554 direction write");
  }
  if (!writeRegister(kOutputRegister, kOutputDefault, "set_level P0|P1|P5=1")) {
    haltForever("FAILED - TCA9554 output write");
  }
  logf("    EPD rail on, audio rail on, battery hold asserted (official values)\n");

  delay(50);  // let the EPD rail settle before touching the panel

  uint8_t inputReg = 0;
  if (readRegister(kInputRegister, inputReg)) {
    logf("[4] readback input reg 0x00 = 0x%02X\n", inputReg);
  } else {
    logf("[4] readback input reg 0x00 failed (evidence only, continuing)\n");
  }

  // --- Step 5: single full refresh. ---
  logf("[5] e-paper init\n");
  if (!display.begin()) {
    logf("    FAILED: %s\n", display.lastError());
    haltForever("FAILED - e-paper initialization");
  }
  logf("    ok\n");

  logf("[6] drawing orientation pattern and refreshing once\n");
  drawOrientationPattern();
  if (!display.present()) {
    logf("    FAILED: %s\n", display.lastError());
    haltForever("FAILED - e-paper refresh");
  }
  logf("    refresh complete\n");

  display.sleep();
  logf("[7] panel in deep sleep\n");

  if (kPowerOffEpdAfterRefresh) {
    // Intentionally disabled for the first smoke run.
    writeRegister(kOutputRegister,
                  static_cast<uint8_t>(kOutputDefault & ~(1U << 0)),
                  "EPD rail off");
  } else {
    logf("[8] EPD rail left powered, matching the official example\n");
  }

  logf("\nEXPECTED IMAGE: 3px border, a solid 30x30 square in the TOP-LEFT,\n");
  logf("and a letter F. If the square is not top-left, or the F is mirrored\n");
  logf("or upside down, the panel orientation differs from the renderer.\n");

  haltForever("SUCCESS - single full refresh completed");
}

void loop() {
  // Unreachable: setup() always ends in haltForever().
}
