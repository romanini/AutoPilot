#include <cstdarg>
#include "FreeRTOS.h"
#include "freertos/portmacro.h"
#include <cstddef>
#include <cmath>
#include <sys/_intsup.h>
#include "AutoPilot.h"
#include <math.h>

#define COMPASS_SHORT_AVERAGE_MAX_SIZE 100
#define COMPASS_LONG_AVERAGE_MAX_SIZE 1000
#define R 6371000.0                           // Earth's mean radius in meters
#define METERS_TO_NAUTICAL_MILES 0.000539957  // Conversion factor: 1 meter = 0.000539957 nautical miles
#define PI std::acos(-1.0)

AutoPilot::AutoPilot(SerialType* ser) {
  serial = ser;

#if defined(ARDUINO_ARCH_ESP32)  // For Arduino Nano ESP32
  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == NULL) {
    serial->println("mutex creation failed");
    while (1)
      ;
  }
#endif

  tmElements_t timeComponents;
  timeComponents.Year = 0;
  timeComponents.Month = 0;
  timeComponents.Day = 0;
  timeComponents.Hour = 0;
  timeComponents.Minute = 0;
  timeComponents.Second = 0;
  dateTime = makeTime(timeComponents);


  motor_stop_time = 0;
  motor_direction = 0;
  fix = false;
  fixquality = 0;
  satellites = 0;

  mode = 0;
  heading_desired = 0.0;
  bearing = 0.0;
  bearing_correction = 0.0;

  heading = 0.0;
  heading_short_average = 0.0;
  heading_long_average = 0.0;
  heading_short_average_change = 0.0;
  heading_long_average_change = 0.0;
  heading_short_average_change_max = 0.0;
  heading_average_initialized = false;
  heading_short_average_size = 0;
  heading_long_average_size = 0;

  start_motor = 0;
  motor_started = false;

  for (int i = 0; i < HEADING_AVERAGE_SIZE; i++) {
    heading_average[i] = 0.0;
  }
  heading_average_count = 0;

  waypoint_set = false;
  waypoint_lat = 0.0;
  waypoint_lon = 0.0;
  location_lat = 0.0;
  location_lon = 0.0;
  course = 0.0;

  steer_angle = 0.0;
  speed = 0.0;
  distance = 0.0;
  destinationChanged = true;
  modeChanged = true;
}

AutoPilot::~AutoPilot() {
#if defined(ARDUINO_ARCH_ESP32)  // For Arduino Nano ESP32
  if (mutex != NULL) {
    vSemaphoreDelete(mutex);
  }
#endif
}

void AutoPilot::setStartMotor(int start_motor) {
  this->lock();
  this->start_motor = start_motor;
  this->motor_started = false;
  this->unlock();
}

int AutoPilot::getStartMotor() {
  this->lock();
  int value = this->start_motor;
  this->unlock();
  return value;
}

void AutoPilot::setMotorStarted(bool motor_started) {
  this->lock();
  this->motor_started = motor_started;
  this->unlock();
}

bool AutoPilot::getMotorStarted() {
  this->lock();
  bool value = this->motor_started;
  this->unlock();
  return value;
}
void AutoPilot::setDateTime(time_t dateTime) {
  this->lock();
  this->dateTime = dateTime;
  this->unlock();
}

time_t AutoPilot::getDateTime() {
  this->lock();
  time_t value = this->dateTime;
  this->unlock();
  return value;
}

int AutoPilot::getMotorStopTime() {
  this->lock();
  int value = this->motor_stop_time;
  this->unlock();
  return value;
}

int AutoPilot::getMotorDirection() {
  this->lock();
  int value = this->motor_direction;
  this->unlock();
  return value;
}

void AutoPilot::setMotor(int motor_stop_time, int motor_direction) {
  this->lock();
  this->motor_stop_time = motor_stop_time;
  this->motor_direction = motor_direction;
  this->unlock();
}

int AutoPilot::getMotorLastRunTime() {
  this->lock();
  int value = this->motor_last_run_time;
  this->unlock();
  return value;
}

void AutoPilot::setMotorLastRunTime(int motor_last_run_time) {
  this->lock();
  this->motor_last_run_time = motor_last_run_time;
  this->unlock();
}

bool AutoPilot::hasFix() {
  this->lock();
  bool value = this->fix;
  this->unlock();
  return value;
}

void AutoPilot::setFix(bool fix) {
  this->lock();
  this->fix = fix;
  if (!fix && mode == 2) {
    setMode(0);
  }
  this->unlock();
}

int AutoPilot::getFixquality() {
  this->lock();
  int value = this->fixquality;
  this->unlock();
  return value;
}

void AutoPilot::setFixquality(int fixquality, int satellites) {
  this->lock();
  this->fixquality = fixquality;
  this->satellites = satellites;
  this->unlock();
}

int AutoPilot::getSatellites() {
  this->lock();
  int value = this->satellites;
  this->unlock();
  return value;
}

int AutoPilot::getMode() {
  this->lock();
  int value = this->mode;
  this->unlock();
  return value;
}

int AutoPilot::setMode(int mode) {
  this->lock();
  int retval = -1;
  if ((mode <= 1) || (mode == 2 && this->waypoint_set)) {
    this->mode = mode;
    if (this->mode == 1) {
      this->heading_desired = this->heading_long_average;
      this->bearing = this->heading_desired;
    }
    this->modeChanged = true;
    this->destinationChanged = true;
    retval = 0;
  }
  this->unlock();
  return retval;
}

float AutoPilot::getHeadingDesired() {
  this->lock();
  float value = this->heading_desired;
  this->unlock();
  return value;
}

void AutoPilot::adjustHeadingDesired(float change) {
  this->lock();
  if (this->mode > 0) {
    if (this->mode == 2) {
      this->heading_desired = this->heading_short_average;
      this->bearing = this->heading_desired;
      this->modeChanged = true;
    }
    this->mode = 1;
    this->heading_desired += change;
    this->bearing = this->heading_desired;
    this->destinationChanged = true;
  }
  this->unlock();
}

float AutoPilot::getBearing() {
  this->lock();
  float value = this->bearing;
  this->unlock();
  return value;
}

float AutoPilot::getBearingCorrection() {
  this->lock();
  float value = this->bearing_correction;
  this->unlock();
  return value;
}

float AutoPilot::getHeading() {
  this->lock();
  float value = this->heading;
  this->unlock();
  return value;
}

void AutoPilot::setHeading(float heading) {
  this->lock();
  if (isnanf(heading) || isnan(heading)) {
    serial->println("received nan Heading!");
    return;
  }
  this->heading = heading;
  if (this->heading_average_initialized) {
    int j = this->heading_average_count % HEADING_AVERAGE_SIZE;
    this->heading_average[j] = heading;
    if (this->heading_average_count < HEADING_AVERAGE_SIZE) {
      this->heading_average_count++;
    } else {
      this->heading_average_count = 0;
    }
    float max_heading = 0.0;
    float min_heading = 360.0;
    float sum_heading = 0.0;
    for (int k = 0; k < HEADING_AVERAGE_SIZE; k++) {
      if (this->heading_average[k] > max_heading) {
        max_heading = this->heading_average[k];
      }
      if (this->heading_average[k] < min_heading) {
        min_heading = this->heading_average[k];
      }
      sum_heading += this->heading_average[k];
    }
    this->heading_long_average = (sum_heading - min_heading - max_heading) / (HEADING_AVERAGE_SIZE - 2);

    // char buff[100];
    // sprintf(buff,"j: %d val: %.2f max: %.2f min: %.2f sum: %.2f, heading: %.2f", j,this->heading_average[j], max_heading, min_heading, sum_heading, this->heading_long_average);
    // serial->println(buff);

  this->heading_short_average = this->heading_short_average + ((this->heading - this->heading_short_average) / this->heading_short_average_size);
  this->heading_short_average_change = this->heading_short_average_change + (((this->heading - this->heading_short_average) - this->heading_short_average_change) / this->heading_short_average_size);
  float change = abs(this->heading - this->heading_short_average);
  if (this->heading_short_average_change_max < change) {
    this->heading_short_average_change_max = change;
  } else {
    this->heading_short_average_change_max -= (this->heading_short_average_change_max - change) / 100.;
  }
}
else {
  this->heading_average_initialized = true;
  this->heading_long_average = heading;
  this->heading_long_average_size = 1;
  this->heading_short_average = heading;
}

if (this->heading_short_average_size < COMPASS_SHORT_AVERAGE_MAX_SIZE) {
  this->heading_short_average_size += 1;
}
if (this->mode == 1) {
  // TODO do we use shot average or long average?
  this->bearing_correction = this->getCourseCorrection(this->bearing, this->heading_long_average);
}
this->unlock();
}

float AutoPilot::getHeadingLongAverage() {
  this->lock();
  float value = this->heading_long_average;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingShortAverage() {
  this->lock();
  float value = this->heading_short_average;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingLongAverageChange() {
  this->lock();
  float value = this->heading_long_average_change;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingShortAverageChange() {
  this->lock();
  float value = this->heading_short_average_change;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingShortAverageChangeMax() {
  this->lock();
  float value = this->heading_short_average_change_max;
  this->unlock();
  return value;
}
float AutoPilot::getHeadingLongAverageSize() {
  this->lock();
  float value = this->heading_long_average_size;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingShortAverageSize() {
  this->lock();
  float value = this->heading_short_average_size;
  this->unlock();
  return value;
}

// float* AutoPilot::getFilteredAccelerometerData() {
//   this->lock();
//   float *value = this->filtered_accelerometer_data;
//   this->unlock();
//   return value;

// }

// void AutoPilot::setFilteredAccelerometerData(float data[3]) {
//   this->lock();
//   for (int i = 0; i < 3; i++) {
//     this->filtered_accelerometer_data[i] = data[i];
//   }
//   this->unlock();
// }

// float* AutoPilot::getFilteredMagentometerData() {
//   this->lock();
//   float *value = this->filtered_magnetometer_data;
//   this->unlock();
//   return value;
// }

// void AutoPilot::setFilteredMagnetometerData(float data[3]) {
//   this->lock();
//   for (int i = 0; i < 3; i++) {
//     this->filtered_magnetometer_data[i] = data[i];
//   }
//   this->unlock();
// }

bool AutoPilot::isWaypointSet() {
  this->lock();
  bool value = this->waypoint_set;
  this->unlock();
  return value;
}

float AutoPilot::getWaypointLat() {
  this->lock();
  float value = this->waypoint_lat;
  this->unlock();
  return value;
}

float AutoPilot::getWaypointLon() {
  this->lock();
  float value = this->waypoint_lon;
  this->unlock();
  return value;
}

void AutoPilot::setWaypoint(float lat, float lon) {
  this->lock();
  this->waypoint_lat = lat;
  this->waypoint_lon = lon;
  this->waypoint_set = true;
  if (this->fix) {
    this->distance = this->getDistance(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
  }
  if (this->mode == 2) {
    this->destinationChanged = true;
  }
  this->unlock();
}

float AutoPilot::getLocationLat() {
  this->lock();
  float value = this->location_lat;
  this->unlock();
  return value;
}

float AutoPilot::getLocationLon() {
  this->lock();
  float value = this->location_lon;
  this->unlock();
  return value;
}

void AutoPilot::setLoation(float lat, float lon, float course) {
  this->lock();
  this->location_lat = lat;
  this->location_lon = lon;
  this->course = course;
  if (this->waypoint_set) {
    this->distance = this->getDistance(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
    if (this->mode == 2) {
      this->bearing = this->getBearing(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
      this->bearing_correction = this->getCourseCorrection(this->bearing, this->course);
    }
  }
  this->unlock();
}

float AutoPilot::getCourse() {
  this->lock();
  float value = this->course;
  this->unlock();
  return value;
}

float AutoPilot::getSpeed() {
  this->lock();
  float value = this->speed;
  this->unlock();
  return value;
}

void AutoPilot::setSpeed(float speed) {
  this->lock();
  this->speed = speed;
  this->unlock();
}

float AutoPilot::getDistance() {
  this->lock();
  float value = this->distance;
  this->unlock();
  return value;
}

float AutoPilot::getSteerAngle() {
  this->lock();
  float value = this->steer_angle;
  this->unlock();
  return value;
}

void AutoPilot::setSteerAngle(float steer_angle) {
  this->lock();
  this->steer_angle = steer_angle;
  this->unlock();
}

void AutoPilot::printAutoPilot() {
  this->lock();
  serial->print("Date&Time: ");
  char dateTimeString[13];
  time_t currentTime = this->dateTime;
  sprintf(dateTimeString, "%d/%d/%02d %d:%02d", month(currentTime), day(currentTime), year(currentTime) % 100, hour(currentTime), minute(currentTime));
  serial->print(dateTimeString);
  if (this->fix) {
    if (this->fixquality == 0) {
      serial->print(" n/a");
    } else if (this->fixquality == 1) {
      serial->print(" GPS");
    } else if (this->fixquality == 2) {
      serial->print(" DGPS");
    }
    serial->print(" (");
    serial->print(this->satellites);
    serial->print(")");
  }
  serial->println("");

  serial->print("Destination: ");
  if (this->mode == 2) {
    serial->print("navigate ");
    serial->print(this->waypoint_lat, 6);
    serial->print(",");
    serial->print(this->waypoint_lon, 6);
  } else if (this->mode == 1) {
    serial->print("compass ");
    serial->print(this->heading_desired, 1);
  } else {
    serial->print("N/A");
  }
  serial->println("");

  serial->print("Heading: ");
  serial->print(this->heading_long_average);
  serial->print(" ~");
  serial->print(this->heading_long_average_change);
  serial->print(" (");
  serial->print(this->heading_long_average_size);
  serial->print(") / ");
  serial->print(this->heading_short_average);
  serial->print(" ~");
  serial->print(this->heading_short_average_change);
  serial->print(" (");
  serial->print(this->heading_short_average_size);
  serial->print(") / ");
  serial->print(this->heading);
  serial->print(" ");
  serial->print("Bearing: ");
  if (this->mode > 0) {
    serial->print(this->bearing, 1);
    serial->print(" ");
    serial->print((this->bearing_correction > 0) ? this->bearing_correction : this->bearing_correction * -1.0, 1);
    serial->print((this->bearing_correction > 0) ? " R" : " L");
  } else {
    serial->print("N/A");
  }
  serial->println("");

  serial->print("Speed: ");
  serial->print(this->speed, 2);
  serial->print(" Distance: ");
  serial->print(this->distance, 2);
  serial->print(" Course: ");
  serial->print(this->course, 2);
  serial->print(" Location: ");
  serial->print(this->location_lat, 6);
  serial->print(",");
  serial->print(this->location_lon, 6);
  serial->println("");
  serial->println("");
  this->unlock();
}

float AutoPilot::toRadians(float degrees) {
  return degrees * PI / 180.0;
}

float AutoPilot::toDegrees(float radians) {
  return radians * 180.0 / PI;
}

float AutoPilot::getCourseCorrection(float bearing, float course) {
  float correction = bearing - course;
  if (correction > 180.0) {
    correction -= 360.0;
  } else if (correction < -180.0) {
    correction += 360.0;
  }
  return correction;
}

// Calculates the distance between two points on the Earth's surface
float AutoPilot::getDistance(float lat1, float lon1, float lat2, float lon2) {
  float dlat = toRadians(float(lat2 - lat1));
  float dlon = toRadians(float(lon2 - lon1));
  float a = sin(dlat / 2) * sin(dlat / 2) + cos(toRadians(lat1)) * cos(toRadians(lat2)) * sin(dlon / 2) * sin(dlon / 2);
  float c = 2 * atan2(sqrt(a), sqrt(1 - a));
  return R * c * METERS_TO_NAUTICAL_MILES;
}

// Calculates the initial bearing from point 1 to point 2
float AutoPilot::getBearing(float lat1, float lon1, float lat2, float lon2) {
  float y = sin(toRadians(lon2 - lon1)) * cos(toRadians(lat2));
  float x = cos(toRadians(lat1)) * sin(toRadians(lat2)) - sin(toRadians(lat1)) * cos(toRadians(lat2)) * cos(toRadians(lon2 - lon1));
  float bearing = toDegrees(atan2(y, x));

  if (bearing < 0) {
    bearing += 360.0;
  }

  if (roundf(bearing * 100) >= 36000) {
    bearing = 0.00;
  }
  return bearing;
}

void AutoPilot::lock() {
#if defined(ARDUINO_ARCH_ESP32)  // For Arduino Nano ESP32
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
#endif
}

void AutoPilot::unlock() {
#if defined(ARDUINO_ARCH_ESP32)  // For Arduino Nano ESP32
  xSemaphoreGiveRecursive(mutex);
#endif
}