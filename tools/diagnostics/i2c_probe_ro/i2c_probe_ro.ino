// READ-ONLY I2C bus probe for Waveshare ESP32-C6-ePaper-1.54
//
// Safety: this sketch NEVER writes a register and never enables any power rail.
// It only measures pin levels and does address-only ACK probes, so it cannot
// re-trigger whatever took the bus down.
//
// Purpose: determine whether the I2C bus lines are held low by an external
// device/short, or whether they are simply floating (nothing connected /
// unpowered), and whether the bus ever recovers over time.

#include <Arduino.h>
#include <Wire.h>

static const uint8_t I2C_SDA = 18;
static const uint8_t I2C_SCL = 8;

// Read a pin under three input modes. Interpretation:
//   pullup=1, pulldown=0            -> line is FLOATING (nothing driving it)
//   pullup=0, pulldown=0            -> line is HELD LOW externally
//   pullup=1, pulldown=1            -> line is DRIVEN HIGH externally
const char* classify(uint8_t pin, int& up, int& down, int& hiz) {
  pinMode(pin, INPUT);
  delayMicroseconds(200);
  hiz = digitalRead(pin);

  pinMode(pin, INPUT_PULLUP);
  delayMicroseconds(500);
  up = digitalRead(pin);

  pinMode(pin, INPUT_PULLDOWN);
  delayMicroseconds(500);
  down = digitalRead(pin);

  pinMode(pin, INPUT);  // leave benign

  if (up == 1 && down == 0) return "FLOATING (no external drive, weak/no pull-up)";
  if (up == 0 && down == 0) return "HELD LOW externally (device or short)";
  if (up == 1 && down == 1) return "DRIVEN/PULLED HIGH externally (healthy bus)";
  return "INCONSISTENT";
}

// Address-only ACK probe. Sends address + STOP, writes no data.
uint8_t probeCount(uint32_t hz, bool printHits) {
  Wire.end();
  delay(5);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(hz);
  Wire.setTimeOut(20);
  delay(5);

  uint8_t found = 0;
  for (uint8_t a = 0x08; a <= 0x77; ++a) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      if (printHits) Serial.printf(" 0x%02X", a);
      found++;
    }
  }
  Wire.end();
  delay(5);
  return found;
}

uint32_t pass = 0;

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(200);
}

void loop() {
  pass++;
  Serial.println();
  Serial.printf("===== READ-ONLY PROBE  pass #%lu  t=%lums =====\n",
                (unsigned long)pass, (unsigned long)millis());

  int up, down, hiz;
  const char* sdaState = classify(I2C_SDA, up, down, hiz);
  Serial.printf("SDA GPIO%-2u : hiz=%d pullup=%d pulldown=%d  -> %s\n",
                I2C_SDA, hiz, up, down, sdaState);

  const char* sclState = classify(I2C_SCL, up, down, hiz);
  Serial.printf("SCL GPIO%-2u : hiz=%d pullup=%d pulldown=%d  -> %s\n",
                I2C_SCL, hiz, up, down, sclState);

  Serial.print("[PROBE 400kHz] ACK:");
  uint8_t n400 = probeCount(400000, true);
  if (!n400) Serial.print(" <none>");
  Serial.printf("  (total=%u)\n", n400);

  Serial.print("[PROBE 100kHz] ACK:");
  uint8_t n100 = probeCount(100000, true);
  if (!n100) Serial.print(" <none>");
  Serial.printf("  (total=%u)\n", n100);

  Serial.println("(read-only: no register was written, no power rail touched)");
  delay(3000);
}
