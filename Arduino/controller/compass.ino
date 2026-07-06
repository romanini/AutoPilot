#include <Arduino.h>
#include <Adafruit_BNO08x.h>
#include <math.h>

#define BNO08X_CS 10
#define BNO08X_INT 9
#define BNO08X_RESET -1

euler_t ypr;

Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

// Rotation-vector report interval, microseconds. Must match how fast the
// consumer drains events: check_compass() runs on control_task's 10 ms loop
// and getSensorEvent() hands back ONE event per call, so asking the sensor
// for anything faster (it was 2000 us = 500 Hz) just backs events up in the
// sensor queue and the heading the PID steers by lags behind the boat.
#define ROTATION_VECTOR_INTERVAL_US 10000  // 100 Hz = the control loop rate

void setReports(void) {
  if (!bno08x.enableReport(SH2_ROTATION_VECTOR, ROTATION_VECTOR_INTERVAL_US)) {
    DEBUG_PRINTLN("Could not enable Gyro Integrated Rotation Vevtor");
  }
  if (!bno08x.enableReport(SH2_STABILITY_CLASSIFIER)) {
    DEBUG_PRINTLN("Could not enable stability classifier");
  }
}

void setup_compass(void) {
  // Try to initialize!
  int attempts = 0;
  boolean bno08Initialized = false;
  while (!bno08Initialized && attempts < 50) {
    bno08Initialized = bno08x.begin_I2C();
    if (!bno08Initialized) {
      attempts++;
      delay(50);
    }
  }
  if (!bno08Initialized) {
    // The compass is essential (heading drives steering), so we can't continue
    // without it. Rather than hang here forever - which would also prevent the
    // display link, GPS, telnet and the FreeRTOS tasks from ever starting -
    // reboot and try again. A transient I2C glitch will clear on the retry.
    DEBUG_PRINTLN("Failed to find BNO08x chip - rebooting to retry");
    if (Serial) Serial.flush();
    delay(1000);  // let the message flush and avoid a tight reboot loop
    ESP.restart();
  } else {
    DEBUG_PRINTLN("BNO08x Found!");
  }
  setReports();

  // get the initial compass reading 
  check_compass();
  // we start in compass mode
  autoPilot.setMode(1);

  DEBUG_PRINTLN("Compass all setup");
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

  ypr->yaw = fmod(350-(ypr->yaw * RAD_TO_DEG) , 360);
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
      case SH2_ROTATION_VECTOR:
        // faster (more noise?)
        quaternionToEuler(sensorValue.un.rotationVector.real,
                          sensorValue.un.rotationVector.i,
                          sensorValue.un.rotationVector.j,
                          sensorValue.un.rotationVector.k, &ypr);
        autoPilot.setHeading(ypr.yaw);
        autoPilot.setPitch(ypr.pitch);
        autoPilot.setRoll(ypr.roll);
        // DEBUG_PRINT("Yaw: ");
        // DEBUG_PRINTLN(autoPilot.getHeading());
        break;
      case SH2_STABILITY_CLASSIFIER: {
        sh2_StabilityClassifier_t stability = sensorValue.un.stabilityClassifier;
        autoPilot.setStabilityClassification((int)stability.classification);
        break;
      }
    }
  }
}
