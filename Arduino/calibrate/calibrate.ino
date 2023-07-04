/***************************************************************************
  This is an example for the Adafruit SensorLab library
  It will look for a supported magnetometer and output
  PJRC Motion Sensor Calibration Tool-compatible serial data

  PJRC & Adafruit invest time and resources providing this open source code,
  please support PJRC and open-source hardware by purchasing products
  from PJRC!

  This "nosave" version does not save any calibration information, which
  means it can be used on boards without an SD card or EEPROM. It sends
  raw IMU data to the serial console to be read by the PJRC MotionCal 
  software (https://www.pjrc.com/store/prop_shield.html) or the Jupyter 
  Notebook in the SensorLab repository:
  https://github.com/adafruit/Adafruit_SensorLab/blob/master/notebooks/Mag_Gyro_Calibration.ipynb.
  
  Written by PJRC, adapted by Limor Fried for Adafruit Industries.
  Modified by Shawn Hymel (January 30, 2022).
 ***************************************************************************/

#include <Adafruit_Sensor.h>
#include <Adafruit_LIS2MDL.h>
#include <Adafruit_LSM303_Accel.h>
#include <Adafruit_SensorLab.h>
#include <Adafruit_Sensor_Calibration.h>

static constexpr double DEGREES_PER_RADIAN = (180.0 / PI); ///< Degrees per radian for conversion

Adafruit_LIS2MDL mag = Adafruit_LIS2MDL(12345);
Adafruit_LSM303_Accel_Unified accel = Adafruit_LSM303_Accel_Unified(54321);

Adafruit_Sensor *gyro = NULL;
sensors_event_t mag_event, gyro_event, accel_event;

int loopcount = 0;

// Hard-iron calibration settings
const float hard_iron[3] = {
  -8.55,  21.21,  -31.25
};

// Soft-iron calibration settings
const float soft_iron[3][3] = {
  {  0.932,  -0.009, 0.011  },
  {  -0.009,  1.040, -0.033  },
  { 0.011, -0.033,  1.033  }
};

void setup(void) {
  Serial.begin(115200);
  while (!Serial) delay(10);     // will pause Zero, Leonardo, etc until serial console opens
  
  Serial.println(F("Sensor Calibration!"));

    /* Initialise the sensor */
  if(!mag.begin())
  {
    /* There was a problem detecting the LIS2MDL ... check your connections */
    Serial.println("Ooops, no LIS2MDL detected ... Check your wiring!");
    while(1);
  }
  mag.printSensorDetails();

  /* Initialise the sensor */
  if (!accel.begin()) {
    /* There was a problem detecting the ADXL345 ... check your connections */
    Serial.println("Ooops, no LSM303 detected ... Check your wiring!");
    while (1)
      ;
  }
  accel.printSensorDetails();

  accel.setRange(LSM303_RANGE_4G);
  accel.setMode(LSM303_MODE_NORMAL);
  
}

void loop() {
  static float hi_cal[3];

  mag.getEvent(&mag_event);
  accel.getEvent(&accel_event);

  // Put raw magnetometer readings into an array
  float mag_data[] = {mag_event.magnetic.x,
                      mag_event.magnetic.y,
                      mag_event.magnetic.z};

  // Apply hard-iron offsets
  for (uint8_t i = 0; i < 3; i++) {
    hi_cal[i] = mag_data[i] - hard_iron[i];
  }

  // Apply soft-iron scaling
  for (uint8_t i = 0; i < 3; i++) {
    mag_data[i] = (soft_iron[i][0] * hi_cal[0]) +
                  (soft_iron[i][1] * hi_cal[1]) +
                  (soft_iron[i][2] * hi_cal[2]);
  }

  // 'Raw' values to match expectation of MOtionCal
  Serial.print("Raw:");
  Serial.print(int(accel_event.acceleration.x*8192/9.8)); Serial.print(",");
  Serial.print(int(accel_event.acceleration.y*8192/9.8)); Serial.print(",");
  Serial.print(int(accel_event.acceleration.z*8192/9.8)); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.print(0); Serial.print(",");
  Serial.print(0); Serial.print(",");
  // Serial.print(int(mag_event.magnetic.x*10)); Serial.print(",");
  // Serial.print(int(mag_event.magnetic.y*10)); Serial.print(",");
  // Serial.print(int(mag_event.magnetic.z*10)); Serial.println("");
  Serial.print(int(mag_data[0]*10)); Serial.print(",");
  Serial.print(int(mag_data[1]*10)); Serial.print(",");
  Serial.print(int(mag_data[2]*10)); Serial.println("");

  // unified data
  Serial.print("Uni:");
  Serial.print(accel_event.acceleration.x); Serial.print(",");
  Serial.print(accel_event.acceleration.y); Serial.print(",");
  Serial.print(accel_event.acceleration.z); Serial.print(",");
  Serial.print(0, 4); Serial.print(",");
  Serial.print(0, 4); Serial.print(",");
  Serial.print(0, 4); Serial.print(",");
  Serial.print(mag_event.magnetic.x); Serial.print(",");
  Serial.print(mag_event.magnetic.y); Serial.print(",");
  Serial.print(mag_event.magnetic.z); Serial.println("");

}
