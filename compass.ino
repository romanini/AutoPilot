#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LIS2MDL.h>
#include <Adafruit_LSM303_Accel.h>
#include "calibrate.h"

unsigned int last_compass_read_time_mills = 0;

Adafruit_LIS2MDL mag = Adafruit_LIS2MDL();
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);

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

float* calibrate(float* data, const float calibration_matrix[][3], const float* bias) {
  static float cal[3];

  // Apply bias offsets
  for (int i = 0; i < 3; i++) {
    cal[i] = data[i] - bias[i];
  }

  // Apply calibration matrix scaling
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      cal[i] += calibration_matrix[i][j] * cal[j];
    }
  }
  //  Normalizing calibrated data
  float norm = sqrt(sq(cal[0]) + sq(cal[1]) + sq(cal[2]));
  for (int i = 0; i < 3; i++) {
    cal[i] = cal[i] / norm;
  }

  return cal;
}

float* read_magnetometer() {

  // Get new sensor event with readings in uTesla
  sensors_event_t event;
  mag.getEvent(&event);

  // Put raw magnetometer readings into an array
  float mag_data[] = { event.magnetic.x, event.magnetic.y, event.magnetic.z };

  return calibrate(mag_data, magnetic_soft_iron, magnetic_hard_iron);
}

float* read_accelletometer() {
  // Get new sensor event with readings
  sensors_event_t event;
  accel.getEvent(&event);

  // Put raw magnetometer readings into an array
  float accel_data[] = { event.acceleration.x, event.acceleration.y, event.acceleration.z };

  return calibrate(accel_data, acceleration_calibration, accelleration_zero_g);
}

void filter(float* data, float* previous_data) {
  for (int i = 0; i < 3; i++) {
    data[i] = (data[i] * LOW_PASS_FILTER_COEFFICIENT) + (previous_data[i] * (1.0 - LOW_PASS_FILTER_COEFFICIENT));
  }
}

// See if it is time to read the compass
void check_compass(AutoPilot& autoPilot) {
  if (millis() - last_compass_read_time_mills > COMPASS_READ_INTERVAL) {
    float* mag_data; 
    mag_data = read_magnetometer();
    float* accel_data; 
    accel_data = read_accelletometer();

    filter(mag_data, autoPilot.getFilteredMagentometerData());
    autoPilot.setFilteredMagnetometerData(mag_data);
    filter(accel_data, autoPilot.getFilteredAccelerometerData());
    autoPilot.setFilteredAccelerometerData(accel_data);

    double pitch = (double)asin((double)-accel_data[0]);
    double roll = (double)asin((double)accel_data[1] / cos((double)pitch));

    //  Calculating tilt compensated heading
    double Xh = mag_data[0] * cos((double)pitch) + mag_data[2] * sin((double)pitch);
    double Yh = mag_data[0] * sin((double)roll) * sin((double)pitch) + mag_data[1] * cos((double)roll) - mag_data[2] * sin((double)roll) * cos((double)pitch);
    float heading = (atan2(Yh, Xh)) * 180 / PI;
    if (heading < 0) {
      heading += 360;
    }
    autoPilot.setHeading(heading);
  }
}