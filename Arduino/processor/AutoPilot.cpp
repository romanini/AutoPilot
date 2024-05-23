#include <cmath>
#include <sys/_intsup.h>
#include "AutoPilot.h"
#include <math.h>

#define COMPASS_SHORT_AVERAGE_MAX_SIZE 100
#define COMPASS_LONG_AVERAGE_MAX_SIZE 1000
#define R 6371000.0                           // Earth's mean radius in meters
#define METERS_TO_NAUTICAL_MILES 0.000539957  // Conversion factor: 1 meter = 0.000539957 nautical miles
#define PI std::acos(-1.0)

AutoPilot::AutoPilot(HardwareSerial* ser) {
  tmElements_t timeComponents;
  timeComponents.Year = 0;
  timeComponents.Month = 0;
  timeComponents.Day = 0;
  timeComponents.Hour = 0;
  timeComponents.Minute = 0;
  timeComponents.Second = 0;
  dateTime = makeTime(timeComponents);

  serial = ser;

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
  heading_average_initialized = false;
  heading_short_average_size = 0;
  heading_long_average_size = 0;

  start_motor = 0;
  motor_started = false;

  for (int i = 0; i < 3; i++) {
    filtered_magnetometer_data[i] = 0.0;
    filtered_accelerometer_data[i] = 0.0;
  }
  waypoint_set = false;
  waypoint_lat = 0.0;
  waypoint_lon = 0.0;
  location_lat = 0.0;
  location_lon = 0.0;
  course = 0.0;

  speed = 0.0;
  distance = 0.0;
  destinationChanged = true;
  modeChanged = true;
}

void AutoPilot::setStartMotor(int start_motor) {
  this->start_motor = start_motor;
  this->motor_started = false;
}

int AutoPilot::getStartMotor() {
  return this->start_motor;
}

void AutoPilot::setMotorStarted(bool motor_started) {
  this->motor_started = motor_started;
}

bool AutoPilot::getMotorStarted() {
  return this->motor_started;
}
void AutoPilot::setDateTime(time_t dateTime) {
  this->dateTime = dateTime;
}

time_t AutoPilot::getDateTime() {
  return this->dateTime;
}

int AutoPilot::getMotorStopTime() {
  return this->motor_stop_time;
}

int AutoPilot::getMotorDirection() {
  return this->motor_direction;
}

void AutoPilot::setMotor(int motor_stop_time, int motor_direction) {
  this->motor_stop_time = motor_stop_time;
  this->motor_direction = motor_direction;
}

int AutoPilot::getMotorLastRunTime() {
  return this->motor_last_run_time;
}

void AutoPilot::setMotorLastRunTime(int motor_last_run_time) {
  this->motor_last_run_time = motor_last_run_time;
}

bool AutoPilot::hasFix() {
  return this->fix;
}

void AutoPilot::setFix(bool fix) {
  this->fix = fix;
  if (!fix && mode == 2) {
    setMode(0);
  }
}

int AutoPilot::getFixquality() {
  return this->fixquality;
}

void AutoPilot::setFixquality(int fixquality, int satellites) {
  this->fixquality = fixquality;
  this->satellites = satellites;
}

int AutoPilot::getSatellites() {
  return this->satellites;
}

int AutoPilot::getMode() {
  return this->mode;
}

int AutoPilot::setMode(int mode) {
  if ((mode <= 1) || (mode == 2 && this->waypoint_set)) {
    this->mode = mode;
    if (this->mode == 1) {
      this->heading_desired = this->heading_short_average;
      this->bearing = this->heading_desired;
    }
    this->modeChanged = true;
    this->destinationChanged = true;
    return 0;
  }
  return -1;
}

float AutoPilot::getHeadingDesired() {
  return this->heading_desired;
}

void AutoPilot::adjustHeadingDesired(float change) {
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
}

float AutoPilot::getBearing() {
  return this->bearing;
}

float AutoPilot::getBearingCorrection() {
  return this->bearing_correction;
}

float AutoPilot::getHeading() {
  return this->heading;
}

void AutoPilot::setHeading(float heading) {
  if (isnanf(heading) || isnan(heading)) {
    serial->println("received nan Heading!");
    return;
  }
  this->heading = heading;
  if (this->heading_average_initialized) {
    this->heading_long_average = this->heading_long_average + ((this->heading - this->heading_long_average) / this->heading_long_average_size);
    this->heading_short_average = this->heading_short_average + ((this->heading - this->heading_short_average) / this->heading_short_average_size);
    this->heading_long_average_change = this->heading_long_average_change + (((this->heading - this->heading_long_average) - this->heading_long_average_change) / this->heading_long_average_size);
    this->heading_short_average_change = this->heading_short_average_change + (((this->heading - this->heading_short_average) - this->heading_short_average_change) / this->heading_short_average_size);
  } else {
    this->heading_average_initialized = true;
    this->heading_long_average = heading;
    this->heading_short_average = heading;
  }
  if (this->heading_long_average_size < COMPASS_LONG_AVERAGE_MAX_SIZE) {
    this->heading_long_average_size += 1;
  }
  if (this->heading_short_average_size < COMPASS_SHORT_AVERAGE_MAX_SIZE) {
    this->heading_short_average_size += 1;
  }
  if (this->mode == 1) {
    // TODO do we use shot average or long average?
    this->bearing_correction = this->getCourseCorrection(this->bearing, this->heading_short_average);
  }
}

float AutoPilot::getHeadingLongAverage() {
  return this->heading_long_average;
}

float AutoPilot::getHeadingShortAverage() {
  return this->heading_short_average;
}

float AutoPilot::getHeadingLongAverageChange() {
  return this->heading_long_average_change;
}

float AutoPilot::getHeadingShortAverageChange() {
  return this->heading_short_average_change;
}

float AutoPilot::getHeadingLongAverageSize() {
  return this->heading_long_average_size;
}

float AutoPilot::getHeadingShortAverageSize() {
  return this->heading_short_average_size;
}

float* AutoPilot::getFilteredAccelerometerData() {
  return this->filtered_accelerometer_data;
}

void AutoPilot::setFilteredAccelerometerData(float data[3]) {
  for (int i = 0; i < 3; i++) {
    this->filtered_accelerometer_data[i] = data[i];
  }
}

float* AutoPilot::getFilteredMagentometerData() {
  return this->filtered_magnetometer_data;
}

void AutoPilot::setFilteredMagnetometerData(float data[3]) {
  for (int i = 0; i < 3; i++) {
    this->filtered_magnetometer_data[i] = data[i];
  }
}

bool AutoPilot::isWaypointSet() {
  return this->waypoint_set;
}

float AutoPilot::getWaypointLat() {
  return this->waypoint_lat;
}

float AutoPilot::getWaypointLon() {
  return this->waypoint_lon;
}

void AutoPilot::setWaypoint(float lat, float lon) {
  this->waypoint_lat = lat;
  this->waypoint_lon = lon;
  this->waypoint_set = true;
  if (this->fix) {
    this->distance = this->getDistance(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
  }
  if (this->mode == 2) {
    this->destinationChanged = true;
  }
}

float AutoPilot::getLocationLat() {
  return this->location_lat;
}

float AutoPilot::getLocationLon() {
  return this->location_lon;
}

void AutoPilot::setLoation(float lat, float lon, float course) {
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
}

float AutoPilot::getCourse() {
  return this->course;
}

float AutoPilot::getSpeed() {
  return this->speed;
}

void AutoPilot::setSpeed(float speed) {
  this->speed = speed;
}

bool AutoPilot::hasDestinationChanged() {
  bool retval = this->destinationChanged;
  this->destinationChanged = false;
  return retval;
}

bool AutoPilot::hasModeChanged() {
  bool retval = this->modeChanged;
  this->modeChanged = false;
  return retval;
}

float AutoPilot::getDistance() {
  return this->distance;
}

void AutoPilot::printAutoPilot() {
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
