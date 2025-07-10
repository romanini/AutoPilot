#include <Wire.h>
#include <Adafruit_BNO08x.h>
#include <EEPROM.h>

#define BNO08X_CS 10
#define BNO08X_INT 9
#define BNO08X_RESET -1

Adafruit_BNO08x bno(BNO08X_RESET);

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);
  int attempts = 0;
  boolean bno08Initialized = false;
  while (!bno08Initialized && attempts < 20) {
    bno08Initialized = bno.begin_I2C();
    if (!bno08Initialized) {
      attempts++;
      delay(50);
    }
  }
  if (!bno08Initialized) {
    Serial.println("Failed to find BNO08x chip");
    while (1) { delay(10); }
  } else {
    Serial.println("BNO08x Found!");
  }
  bno.enableReport(BNO_REPORT_ROTATION_VECTOR);

  Serial.println("Calibrating... wave in figure-8 until all values = 3");
  // 1) wait for full calibration
  while (true) {
    uint8_t sys, gyro, accel, mag;
    bno.getCalibration(&sys, &gyro, &accel, &mag);
    Serial.printf("SYS=%u GYRO=%u ACC=%u MAG=%u\n", sys, gyro, accel, mag);
    if (sys==3 && gyro==3 && accel==3 && mag==3) break;
    delay(500);
  }
  uint8_t offsets[22];
  // 2) grab offsets from the sensor
  bno.getSensorOffsets(offsets);
  Serial.println("Got offsets; writing to EEPROM...");

  // 3) write them into EEPROM addresses 0..21
  for (uint8_t i = 0; i < 22; i++) {
    EEPROM.update(i, offsets[i]);
  }
  Serial.println("Done! Power-cycle or reset and switch to autoPilot sketch.");
}

void loop() {
  // nothing else to do here
}

