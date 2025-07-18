#if defined(ARDUINO_ARCH_SAMD)
#include <avr/pgmspace.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#else
#error "Unsupported board type. Please use an AVR, SAMD, or ESP32 based board."
#endif
#include <cstdlib>
#include <cstring>
#include "AutoPilot.h"

#define BATTERY_VOLTS_AVERAGE_MAX_SIZE 12
#define INPUT_VOLTS_AVERAGE_MAX_SIZE 12

AutoPilot::AutoPilot(SerialType *ser) {
  serial = ser;
  battery_voltage = 0.0;
  battery_voltage_average_size = 0;
  input_voltage = 0.0;
  input_voltage_average_size = 0;
  this->init();
}

void AutoPilot::init() {
  year = 0;
  month = 0;
  day = 0;
  hour = 0;
  minute = 0;
  fix = false;
  fixquality = 0;
  satellites = 0;
  navigation_enabled = false;
  mode = 0;
  waypoint_set = false;
  waypoint_lat = 0.0;
  waypoint_lon = 0.0;
  heading_desired = 0.0;
  heading = 0.0;
  pitch = 0.0;
  roll = 0.0;
  bearing = 0.0;
  bearing_correction = 0.0;
  speed = 0.0;
  distance = 0.0;
  course = 0.0;
  location_lat = 0.0;
  location_lon = 0.0;
  destinationChanged = true;
  modeChanged = true;
  reset = false;
  connected = false;
  tackRequested = 0;
}

int AutoPilot::getYear() {
  return this->year;
}

int AutoPilot::getMonth() {
  return this->month;
}

int AutoPilot::getDay() {
  return this->day;
}

int AutoPilot::getHour() {
  return this->hour;
}

int AutoPilot::getMinute() {
  return this->minute;
}

bool AutoPilot::hasFix() {
  return this->fix;
}

int AutoPilot::getFixquality() {
  return this->fixquality;
}

int AutoPilot::getSatellites() {
  return this->satellites;
}

int AutoPilot::getMode() {
  return this->mode;
}

void AutoPilot::setMode(int mode) {
  this->mode = mode;
}

bool AutoPilot::isNavigationEnabled() {
  return this->navigation_enabled;
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

float AutoPilot::getHeadingDesired() {
  return this->heading_desired;
}

float AutoPilot::getHeading() {
  return this->heading;
}

float AutoPilot::getPitch() {
  return this->pitch;
}

float AutoPilot::getRoll() {
  return this->roll;
}

int AutoPilot::getStabilityClassification() {
  return this->stability_classification;
}

float AutoPilot::getBearing() {
  return this->bearing;
}

float AutoPilot::getBearingCorrection() {
  return this->bearing_correction;
}

float AutoPilot::getSpeed() {
  return this->speed;
}

float AutoPilot::getDistance() {
  return this->distance;
}

float AutoPilot::getCourse() {
  return this->course;
}

float AutoPilot::getLocationLat() {
  return this->location_lat;
}

float AutoPilot::getLocationLon() {
  return this->location_lon;
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

float AutoPilot::getBatteryVoltage() {
  return this->battery_voltage;
}

void AutoPilot::setBatteryVoltage(float voltage) {
  if (voltage > 0.2) {
    if (this->battery_voltage_average_size < BATTERY_VOLTS_AVERAGE_MAX_SIZE) {
      this->battery_voltage_average_size += 1;
    }
    this->battery_voltage = this->battery_voltage + ((voltage - this->battery_voltage) / this->battery_voltage_average_size);
  } else {
    this->battery_voltage = 0.0;
    this->battery_voltage_average_size = 0;
  }
}

float AutoPilot::getInputVoltage() {
  return this->input_voltage;
}

void AutoPilot::setInputVoltage(float voltage) {
  if (voltage > 0.2) {
    if (this->input_voltage_average_size < INPUT_VOLTS_AVERAGE_MAX_SIZE) {
      this->input_voltage_average_size += 1;
    }
    this->input_voltage = this->input_voltage + ((voltage - this->input_voltage) / this->input_voltage_average_size);
  } else {
    this->input_voltage = 0.0;
    this->input_voltage_average_size = 0;
  }
}

bool AutoPilot::getReset() {
  return this->reset;
}

void AutoPilot::setReset(bool value) {
  this->reset = value;
}

bool AutoPilot::isConnected() {
  return this->connected;
}

void AutoPilot::setConnected(bool connected) {
  if (this->connected != connected) {
    this->modeChanged = true;
    this->destinationChanged = true;
  }
  this->connected = connected;
}

unsigned long AutoPilot::getTackRequested() {
  return this->tackRequested;
}

void AutoPilot::setTackRequested(unsigned long time) {
  this->tackRequested = time;
  this->modeChanged = true;
}

void AutoPilot::cancelTackRequested() {
  this->tackRequested = 0;
  this->modeChanged = true;
}

bool AutoPilot::isTackRequested() {
  return (this->tackRequested > 0);
}

void AutoPilot::printAutoPilot() {
  serial->print("Date&Time: ");
  char dateTimeString[13];
  sprintf(dateTimeString, "%d/%d/%02d %d:%02d", this->month, this->day, this->year, this->hour, this->minute);
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

void AutoPilot::parse(char *sentence) {
  if (strncmp(sentence, APDAT, 6) == 0) {
    parseAPDAT(sentence);
    setConnected(true);
  } else if (strncmp(sentence, RESET, 6) == 0) {
    parseRESET(sentence);
    setConnected(true);
  } else {
    serial->print("unknown sentence");
  }
}

void AutoPilot::parseAPDAT(char *sentence) {
  char *p = sentence;  // Pointer to move through the sentence -- good parsers are non-destructive

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->year = atoi(p);
  } else {
    this->year = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->month = atoi(p);
  } else {
    this->month = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->day = atoi(p);
  } else {
    this->day = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->hour = atoi(p);
  } else {
    this->hour = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->minute = atoi(p);
  } else {
    this->minute = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    int fix = atoi(p);
    if (fix > 0) {
      this->fix = true;
    } else {
      this->fix = false;
    }
  } else {
    this->fix = false;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->fixquality = atoi(p);
  } else {
    this->fixquality = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->satellites = atoi(p);
  } else {
    this->satellites = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  int currentNavigationEnabled = this->navigation_enabled;
  if (!isEmpty(p)) {
    int navigation = atoi(p);
    if (navigation > 0) {
      this->navigation_enabled = true;
    } else {
      this->navigation_enabled = false;
    }
  } else {
    this->navigation_enabled = false;
  }
  if (currentNavigationEnabled != this->navigation_enabled) {
    this->destinationChanged = true;
    this->modeChanged = true;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  int currentMode = this->mode;
  if (!isEmpty(p)) {
    this->mode = atoi(p);
  } else {
    this->mode = 1;
  }
  if (currentMode != this->mode) {
    this->destinationChanged = true;
    this->modeChanged = true;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  int currentWaypointSet = this->waypoint_set;
  if (!isEmpty(p)) {
    int waypoint_set = atoi(p);
    if (waypoint_set > 0) {
      this->waypoint_set = true;
    } else {
      this->waypoint_set = false;
    }
  } else {
    this->waypoint_set = false;
  }
  if ((currentWaypointSet != this->waypoint_set) && (this->mode == 2)) {
    this->destinationChanged = true;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  float currentWaypointLat = this->waypoint_lat;
  if (!isEmpty(p)) {
    this->waypoint_lat = atof(p);
  } else {
    this->waypoint_lat = 0.0;
  }
  if ((currentWaypointLat != this->waypoint_lat) && (this->mode == 2)) {
    this->destinationChanged = true;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  float currentWaypointLon = this->waypoint_lon;
  if (!isEmpty(p)) {
    this->waypoint_lon = atof(p);
  } else {
    this->waypoint_lon = 0.0;
  }
  if ((currentWaypointLon != this->waypoint_lon) && (this->mode == 2)) {
    this->destinationChanged = true;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  float currentHeadingDesired = this->heading_desired;
  if (!isEmpty(p)) {
    this->heading_desired = atof(p);
  } else {
    this->heading_desired = 0.0;
  }
  if ((currentHeadingDesired != this->heading_desired) && (this->mode == 1)) {
    this->destinationChanged = true;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->heading = atof(p);
  } else {
    this->heading = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->pitch = atof(p);
  } else {
    this->pitch = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->roll = atof(p);
  } else {
    this->roll = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->stability_classification = atoi(p);
  } else {
    this->stability_classification = 0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->bearing = atof(p);
  } else {
    this->bearing = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->bearing_correction = atof(p);
  } else {
    this->bearing_correction = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->speed = atof(p);
  } else {
    this->speed = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->distance = atof(p);
  } else {
    this->distance = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->course = atof(p);
  } else {
    this->course = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->location_lat = atof(p);
  } else {
    this->location_lat = 0.0;
  }

  p = strchr(p, ',') + 1;  // Skip to char after the next comma, then check.
  if (!isEmpty(p)) {
    this->location_lon = atof(p);
  } else {
    this->location_lon = 0.0;
  }
}

// For now the only thing to reset is the command connection
// some day when there are other things to reset we can
// parse this and know what we need to reset.
void AutoPilot::parseRESET(char *sentence) {
  this->reset = true;
}

/**************************************************************************/
/*!
    @brief Is the field empty, or should we try conversion? Won't work
    for a text field that starts with an asterisk or a comma, but that
    probably violates the NMEA-183 standard.
    @param pStart Pointer to the location of the token in the NMEA string
    @return true if empty field, false if something there
*/
/**************************************************************************/
bool AutoPilot::isEmpty(char *pStart) {
  if (',' != *pStart && '*' != *pStart && pStart != NULL)
    return false;
  else
    return true;
}
