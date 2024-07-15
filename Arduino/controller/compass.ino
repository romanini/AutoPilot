#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LIS2MDL.h>
#include <Adafruit_LSM303_Accel.h>
#include "calibrate.h"
#include "math.h"

uint32_t last_compass_read_time_mills = millis();

Adafruit_LIS2MDL mag = Adafruit_LIS2MDL(12345);
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);

float mag_data[3];
float accel_data[3];

void setup_compass() {
  /* Initialise the sensor */
  if (!mag.begin()) {
    /* There was a problem detecting the LIS2MDL ... check your connections */
    Serial.println("Ooops, no LIS3MDL detected ... Check your wiring!");
    while (1)
      ;
  }

  /* Initialise the sensor */
  if (!accel.begin()) {
    /* There was a problem detecting the ADXL345 ... check your connections */
    Serial.println("Ooops, no LSM303 detected ... Check your wiring!");
    while (1)
      ;
  }

  accel.setRange(LSM303_RANGE_4G);
  accel.setMode(LSM303_MODE_NORMAL);

  Serial.println("Compass all setup");
}

void read_magnetometer() {

  // Get new sensor event with readings in uTesla
  sensors_event_t event;
  mag.getEvent(&event);

  // Put raw magnetometer readings into an array
  float data[3] = { event.magnetic.x, event.magnetic.y, event.magnetic.z };

  for (int i = 0; i < 3; i++) {
    mag_data[i] = data[i] - magnetic_hard_iron[i];
  }

  // Apply calibration matrix scaling
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      mag_data[i] += magnetic_soft_iron[i][j] * mag_data[j];
    }
  }
  //  Normalizing calibrated data
  float norm = sqrt(sq(mag_data[0]) + sq(mag_data[1]) + sq(mag_data[2]));
  for (int i = 0; i < 3; i++) {
    mag_data[i] = mag_data[i] / norm;
  }
}


void read_accelletometer() {
  // Get new sensor event with readings
  sensors_event_t accel_event;
  accel.getEvent(&accel_event);

  // Put raw magnetometer readings into an array
  float data[3] = { accel_event.acceleration.x, accel_event.acceleration.y, accel_event.acceleration.z };

  // Apply bias offsets
  for (int i = 0; i < 3; i++) {
    accel_data[i] = data[i] - accelleration_zero_g[i];
  }

  // Apply calibration matrix scaling
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      accel_data[i] += acceleration_calibration[i][j] * accel_data[j];
    }
  }
  //  Normalizing calibrated data
  float norm = sqrt(sq(accel_data[0]) + sq(accel_data[1]) + sq(accel_data[2]));
  for (int i = 0; i < 3; i++) {
    accel_data[i] = accel_data[i] / norm;
  }
}


// See if it is time to read the compass
void check_compass() {
  if (millis() - last_compass_read_time_mills > COMPASS_READ_INTERVAL) {
    last_compass_read_time_mills = millis();

    read_magnetometer();

    float* previous_mag_data = autoPilot.getFilteredMagentometerData();
    for (int i = 0; i < 3; i++) {
      mag_data[i] = (mag_data[i] * LOW_PASS_FILTER_COEFFICIENT) + (previous_mag_data[i] * (1.0 - LOW_PASS_FILTER_COEFFICIENT));
    }
    autoPilot.setFilteredMagnetometerData(mag_data);

    read_accelletometer();

    float* previous_accel_data = autoPilot.getFilteredAccelerometerData();
    for (int i = 0; i < 3; i++) {
      accel_data[i] = (accel_data[i] * LOW_PASS_FILTER_COEFFICIENT) + (previous_accel_data[i] * (1.0 - LOW_PASS_FILTER_COEFFICIENT));
    }
    autoPilot.setFilteredAccelerometerData(accel_data);

    double roll = atan2(accel_data[1], accel_data[2]);
    double pitch = atan2((accel_data[0] * -1.0), sqrt(accel_data[1] * accel_data[1] + accel_data[2] * accel_data[2]));

    double x = accel_data[0] * -1.0;
    //double y = std::asin(x);
    //double z = asin(x);
    //double pitch = asin(accel_data[0] * -1.0);
    // double roll = (double)asin((double)accel_data[1] / cos((double)pitch));

    // //  Calculating tilt compensated heading
    double Xh = mag_data[0] * cos((double)pitch) + mag_data[2] * sin((double)pitch);
    double Yh = mag_data[0] * sin((double)roll) * sin((double)pitch) + mag_data[1] * cos((double)roll) - mag_data[2] * sin((double)roll) * cos((double)pitch);
    float heading = (atan2(Xh, Yh)) * 180 / PI;
    if (heading < 0) {
      heading += 360;
    }
    autoPilot.setHeading(heading);
  }
}