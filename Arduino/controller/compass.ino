#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

#define BNO08X_CS 10
#define BNO08X_INT 9
#define BNO08X_RESET -1

euler_t ypr;

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

void setReports(void) {
  if (!bno08x.enableReport(SH2_GYRO_INTEGRATED_RV, 2000)) {
    Serial.println("Could not enable Gyro Integrated Rotation Vevtor");
  }
  if (!bno08x.enableReport(SH2_STABILITY_CLASSIFIER)) {
    Serial.println("Could not enable stability classifier");
  }
}

void setup_compass(void) {
  // Try to initialize!
  int attempts = 0;
  boolean bno08Initialized = false;
  while (!bno08Initialized && attempts < 20) {
    bno08Initialized = bno08x.begin_I2C();
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
  setReports();

  // get the initial compass reading 
  check_compass();
  // we start in compass mode
  autoPilot.setMode(1);

  Serial.println("Compass all setup");
  delay(100);
}

void quaternionToEuler(float qr, float qi, float qj, float qk, euler_t* ypr) {

  float sqr = sq(qr);
  float sqi = sq(qi);
  float sqj = sq(qj);
  float sqk = sq(qk);

  ypr->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
  ypr->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
  ypr->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

  ypr->yaw = fmod((ypr->yaw * RAD_TO_DEG) + 360, 360);
  ypr->pitch = fmod((ypr->pitch * RAD_TO_DEG) + 360, 360);
  ypr->roll = fmod((ypr->roll * RAD_TO_DEG) + 360, 360);
}

void check_compass() {
  if (bno08x.wasReset()) {
    DEBUG_PRINTLN("sensor was reset ");
    setReports();
  }

  if (bno08x.getSensorEvent(&sensorValue)) {
    // in this demo only one report type will be received depending on FAST_MODE define (above)
    switch (sensorValue.sensorId) {
      case SH2_GYRO_INTEGRATED_RV:
        // faster (more noise?)
        quaternionToEuler(sensorValue.un.gyroIntegratedRV.real,
                          sensorValue.un.gyroIntegratedRV.i,
                          sensorValue.un.gyroIntegratedRV.j,
                          sensorValue.un.gyroIntegratedRV.k, &ypr);
        autoPilot.setHeading(ypr.yaw);
        autoPilot.setPitch(ypr.pitch);
        autoPilot.setRoll(ypr.roll);
        // DEBUG_PRINT("Yaw: ");
        // DEBUG_PRINTLN(autoPilot.getHeading());
        break;
      case SH2_STABILITY_CLASSIFIER:
        sh2_StabilityClassifier_t stability = sensorValue.un.stabilityClassifier;
        autoPilot.setStabilityClassification((int)stability.classification);
        break;
    }
  }
}
