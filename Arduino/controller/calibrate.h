#ifndef CALIBRATE_H
#define CALIBRATE_H


#define COMPASS_READ_INTERVAL 20          // read at 50Hz for high accuracy
#define LOW_PASS_FILTER_COEFFICIENT 0.25  // low pass smoothing factor

// Hard-iron magnetic calibration settings
const float magnetic_hard_iron[3] = {
  -30.70, 20.52, 63.24
};

// Soft-iron magnetic calibration settings
const float magnetic_soft_iron[3][3] = {
  { 1.010, 0.015, 0.004 },
  { 0.015, 0.998, -0.035 },
  { 0.004, -0.035, 0.993 }
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