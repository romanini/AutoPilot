#ifndef CALIBRATE_H
#define CALIBRATE_H


#define COMPASS_READ_INTERVAL 20          // read at 50Hz for high accuracy
#define LOW_PASS_FILTER_COEFFICIENT 0.25  // low pass smoothing factor

// Hard-iron magnetic calibration settings
const float magnetic_hard_iron[3] = {
  40.21, 30.01, -26.28
};

// Soft-iron magnetic calibration settings
const float magnetic_soft_iron[3][3] = {
  { 0.965, 0.009, -0.020 },
  { 0.009, 1.030, -0.028 },
  { -0.020, -0.028, 1.007 }
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