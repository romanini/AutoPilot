#ifndef CALIBRATE_H
#define CALIBRATE_H

#define MIN_MOTOR_OFF_TIME 100
#define MILLIS_PER_DEGREE_CORRECTION 100

#define COMPASS_READ_INTERVAL 20          // read at 50Hz for high accuracy
#define LOW_PASS_FILTER_COEFFICIENT 0.25  // low pass smoothing factor

// Hard-iron magnetic calibration settings
const float magnetic_hard_iron[3] = {
  -8.55, 21.21, -31.25
};

// Soft-iron magnetic calibration settings
const float magnetic_soft_iron[3][3] = {
  { 0.932, -0.009, 0.011 },
  { -0.009, 1.040, -0.033 },
  { 0.011, -0.033, 1.033 }
};

const float  acceleration_calibration[3][3] = {
  { 1.0, 0.0, 0.0 },
  { 0.0, 1.0, 0.0 },
  { 0.0, 0.0, 1.0 }
};
//zero-g[3] is the zero-g offset
//replace ACC10(Bx), ACC20(By), ACC30(Bz) with your zero-g offset data
const float accelleration_zero_g[3] = {
  0.0, 0.0, 0.0
};

#endif