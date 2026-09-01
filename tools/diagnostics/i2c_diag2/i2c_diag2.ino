// I2C / TCA9554 diagnostic v2 for Waveshare ESP32-C6-ePaper-1.54
// Mock-safe: no Wi-Fi, no extra library (Wire is core), does NOT drive the panel.
//
// v2 change: the FULL diagnostic runs inside loop() and repeats forever, so the
// output can never be missed by connecting the Serial Monitor too late. No RST
// button is required on this board.
//
// Pins mirror the project exactly (board_config.h):
//   I2C SDA = GPIO18, I2C SCL = GPIO8, TCA9554 @ 0x20
//   P0 = e-paper power (active high), P1 = audio, P5 = battery hold

#include <Arduino.h>
#include <Wire.h>

static const uint8_t I2C_SDA = 18;
static const uint8_t I2C_SCL = 8;
static const uint8_t TCA_ADDR = 0x20;

static const uint8_t REG_INPUT = 0x00;
static const uint8_t REG_OUTPUT = 0x01;
static const uint8_t REG_CONFIG = 0x03;

static const uint8_t P0_EPD_POWER = 1 << 0;
static const uint8_t P1_AUDIO = 1 << 1;
static const uint8_t P5_BAT_HOLD = 1 << 5;

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

uint8_t writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(TCA_ADDR);
  Wire.write(reg);
  Wire.write(val);
  return Wire.endTransmission();
}

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
  Serial.printf("[SCAN %s] ACK:", tag);
  uint8_t found = 0;
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  if (!found) Serial.print(" <none>");
  Serial.printf("  (total=%u)\n", found);
}

void dumpRegs(const char* tag) {
  uint8_t v, e;
  Serial.printf("[REGS %s]", tag);
  if (readReg(REG_INPUT, v, e))  Serial.printf(" in=0x%02X", v);
  else                           Serial.printf(" in=ERR(%s)", txName(e));
  if (readReg(REG_OUTPUT, v, e)) Serial.printf(" out=0x%02X", v);
  else                           Serial.printf(" out=ERR(%s)", txName(e));
  if (readReg(REG_CONFIG, v, e)) Serial.printf(" cfg=0x%02X", v);
  else                           Serial.printf(" cfg=ERR(%s)", txName(e));
  Serial.println();
}

// Check the raw idle levels of the bus lines. A line stuck low with no
// transaction in flight means the bus is held down by a device or a short.
void checkIdleLevels() {
  Wire.end();
  delay(5);
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  delay(5);
  const int sda = digitalRead(I2C_SDA);
  const int scl = digitalRead(I2C_SCL);
  Serial.printf("[LINES] idle SDA(GPIO%u)=%d SCL(GPIO%u)=%d  %s\n",
                I2C_SDA, sda, I2C_SCL, scl,
                (sda == 1 && scl == 1) ? "both HIGH = bus idle OK"
                                       : "LOW = BUS HELD DOWN");
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(20);
  delay(5);
}

uint32_t pass = 0;

void setup() {
  Serial.begin(115200);
  // Never block on USB CDC writes when no host is attached.
  Serial.setTxTimeoutMs(0);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(20);
  delay(200);
}

void loop() {
  pass++;
  Serial.println();
  Serial.printf("===== TCA9554 DIAG v2  pass #%lu  t=%lums =====\n",
                (unsigned long)pass, (unsigned long)millis());
  Serial.printf("SDA=GPIO%u SCL=GPIO%u TCA=0x%02X\n", I2C_SDA, I2C_SCL, TCA_ADDR);

  checkIdleLevels();
  scanBus("start");
  dumpRegs("start");

  // Replicate the project's begin(): levels first, then directions.
  uint8_t out = 0xFF, cfg = 0xFF, e = 0;
  if (!readReg(REG_OUTPUT, out, e)) {
    Serial.printf("[BEGIN] read output FAILED: %s (assuming 0xFF)\n", txName(e));
    out = 0xFF;
  }
  if (!readReg(REG_CONFIG, cfg, e)) {
    Serial.printf("[BEGIN] read config FAILED: %s (assuming 0xFF)\n", txName(e));
    cfg = 0xFF;
  }

  out |= P0_EPD_POWER;
  out |= P5_BAT_HOLD;
  out &= (uint8_t)~P1_AUDIO;
  Serial.printf("[BEGIN] write output=0x%02X -> %s\n", out, txName(writeReg(REG_OUTPUT, out)));

  cfg &= (uint8_t)~(P0_EPD_POWER | P1_AUDIO | P5_BAT_HOLD);
  Serial.printf("[BEGIN] write config=0x%02X -> %s\n", cfg, txName(writeReg(REG_CONFIG, cfg)));
  delay(20);

  dumpRegs("afterBegin");

  // This is the call that fails in the dashboard firmware.
  Serial.println("--- replicating setEpaper(true) 3x ---");
  for (uint8_t i = 0; i < 3; ++i) {
    uint8_t cur = out, err = 0;
    bool haveCur = readReg(REG_OUTPUT, cur, err);
    if (!haveCur) Serial.printf("  [%u] readback FAILED: %s\n", i, txName(err));
    const uint8_t next = (uint8_t)(cur | P0_EPD_POWER);
    const uint8_t w = writeReg(REG_OUTPUT, next);
    Serial.printf("  [%u] setEpaper(true): write 0x%02X -> %s\n", i, next, txName(w));
    delay(100);
  }

  scanBus("end");
  Serial.println("===== end of pass, repeating in 3s =====");
  delay(3000);
}
