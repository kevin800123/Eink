// I2C / TCA9554 diagnostic for Waveshare ESP32-C6-ePaper-1.54
// Mock-safe: no Wi-Fi, no extra library (Wire is core), does NOT drive the panel.
// Goal: find out WHY board_power setEpaper() write to TCA9554 fails after boot.
//
// Pins mirror the project exactly (board_config.h):
//   I2C SDA = GPIO18, I2C SCL = GPIO8, TCA9554 @ 0x20
//   P0 = e-paper power (active high)

#include <Arduino.h>
#include <Wire.h>

static const uint8_t I2C_SDA = 18;
static const uint8_t I2C_SCL = 8;
static const uint8_t TCA_ADDR = 0x20;

static const uint8_t REG_INPUT  = 0x00;
static const uint8_t REG_OUTPUT = 0x01;
static const uint8_t REG_CONFIG = 0x03;

static const uint8_t P0_EPD_POWER = 1 << 0;

// endTransmission() codes:
// 0 = success, 1 = data too long, 2 = NACK on address,
// 3 = NACK on data, 4 = other, 5 = timeout (bus stuck)
const char* txName(uint8_t c) {
  switch (c) {
    case 0: return "OK";
    case 1: return "DATA_TOO_LONG";
    case 2: return "NACK_ADDR(no device ack)";
    case 3: return "NACK_DATA";
    case 4: return "OTHER";
    case 5: return "TIMEOUT(bus stuck low)";
    default: return "UNKNOWN";
  }
}

// Write one register, return raw endTransmission code.
uint8_t writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission();
}

// Read one register. Returns true on success and stores value.
bool readReg(uint8_t reg, uint8_t& out, uint8_t& errPhase) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(reg);
  uint8_t e = Wire.endTransmission(false);  // repeated start
  if (e != 0) { errPhase = e; return false; }
  uint8_t n = Wire.requestFrom(TCA_ADDR, (uint8_t)1);
  if (n != 1) { errPhase = 99; return false; }
  out = Wire.read();
  return true;
}

void scanBus(const char* tag) {
  Serial.printf("[SCAN %s] devices ACKing on I2C:", tag);
  uint8_t found = 0;
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  if (!found) Serial.print(" <none>");
  Serial.printf("   (total=%u)\n", found);
}

void dumpRegs(const char* tag) {
  uint8_t v, e;
  Serial.printf("[REGS %s]", tag);
  if (readReg(REG_INPUT, v, e))  Serial.printf(" input(0x00)=0x%02X", v);
  else                           Serial.printf(" input(0x00)=ERR(%s)", txName(e));
  if (readReg(REG_OUTPUT, v, e)) Serial.printf(" output(0x01)=0x%02X", v);
  else                           Serial.printf(" output(0x01)=ERR(%s)", txName(e));
  if (readReg(REG_CONFIG, v, e)) Serial.printf(" config(0x03)=0x%02X", v);
  else                           Serial.printf(" config(0x03)=ERR(%s)", txName(e));
  Serial.println();
}

void setup() {
  Serial.begin(115200);
  delay(1500);
  Serial.println();
  Serial.println("=== TCA9554 / I2C DIAGNOSTIC START ===");
  Serial.printf("SDA=GPIO%u  SCL=GPIO%u  TCA=0x%02X\n", I2C_SDA, I2C_SCL, TCA_ADDR);

  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  delay(10);

  scanBus("initial@400k");
  dumpRegs("initial");

  // Replicate project begin(): set P0(EPD)+P5(bat hold) high, clear P1(audio),
  // then set those pins as outputs (config bit 0 = output).
  uint8_t out, cfg, e;
  if (!readReg(REG_OUTPUT, out, e)) { Serial.printf("read output failed: %s\n", txName(e)); out = 0xFF; }
  if (!readReg(REG_CONFIG, cfg, e)) { Serial.printf("read config failed: %s\n", txName(e)); cfg = 0xFF; }

  out |= (1 << 0);   // P0 e-paper power high
  out |= (1 << 5);   // P5 battery hold high
  out &= ~(1 << 1);  // P1 audio low
  uint8_t w1 = writeReg(REG_OUTPUT, out);
  Serial.printf("[BEGIN] write output=0x%02X -> %s\n", out, txName(w1));

  cfg &= ~((1 << 0) | (1 << 1) | (1 << 5));  // those pins = output
  uint8_t w2 = writeReg(REG_CONFIG, cfg);
  Serial.printf("[BEGIN] write config=0x%02X -> %s\n", cfg, txName(w2));
  delay(20);

  scanBus("afterBegin");
  dumpRegs("afterBegin");
  Serial.println("--- now toggling P0 (EPD power) every 800ms, printing exact codes ---");
}

uint32_t n = 0;

void loop() {
  bool on = (n % 2) == 0;
  uint8_t out, e;
  bool haveOut = readReg(REG_OUTPUT, out, e);
  if (!haveOut) {
    Serial.printf("#%lu readOutput FAIL: %s | ", (unsigned long)n, txName(e));
    out = on ? P0_EPD_POWER : 0x00;  // fall back
  }
  uint8_t next = on ? (out | P0_EPD_POWER) : (out & ~P0_EPD_POWER);
  uint8_t w = writeReg(REG_OUTPUT, next);
  Serial.printf("#%lu setP0=%d write 0x%02X -> %s", (unsigned long)n, on, next, txName(w));
  if (w != 0) {
    // On failure, re-scan to see if the device is still on the bus at all.
    Serial.print("  ");
    scanBus("onFail");
  } else {
    Serial.println();
  }
  n++;
  delay(800);
}
