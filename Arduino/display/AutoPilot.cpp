#if defined(ARDUINO_ARCH_SAMD)
#include <avr/pgmspace.h>
#elif defined(ARDUINO_ARCH_ESP32)
#include <pgmspace.h>
#else
#error "Unsupported board type. Please use an AVR, SAMD, or ESP32 based board."
#endif
#include <Arduino.h>
#include <cstdlib>
#include <cstring>
#include "AutoPilot.h"

#define BATTERY_VOLTS_AVERAGE_MAX_SIZE 12
#define INPUT_VOLTS_AVERAGE_MAX_SIZE 12

AutoPilot::AutoPilot(SerialType *ser) {
  serial = ser;

  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == NULL) {
    serial->println("mutex creation failed");
    while (1)
      ;
  }

  battery_voltage = 0.0;
  battery_voltage_average_size = 0;
  input_voltage = 0.0;
  input_voltage_average_size = 0;
  this->init();
}

AutoPilot::~AutoPilot() {
  if (mutex != NULL) {
    vSemaphoreDelete(mutex);
  }
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
  mode = 1;
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
  localCommandTime = 0;
}

int AutoPilot::getYear() {
  this->lock();
  int value =this->year;
  this->unlock();
  return value;
}

int AutoPilot::getMonth() {
  this->lock();
  int value =this->month;
  this->unlock();
  return value;
}

int AutoPilot::getDay() {
  this->lock();
  int value =this->day;
  this->unlock();
  return value;
}

int AutoPilot::getHour() {
  this->lock();
  int value =this->hour;
  this->unlock();
  return value;
}

int AutoPilot::getMinute() {
  this->lock();
  int value =this->minute;
  this->unlock();
  return value;
}

bool AutoPilot::hasFix() {
  this->lock();
  bool value =this->fix;
  this->unlock();
  return value;
}

int AutoPilot::getFixquality() {
  this->lock();
  int value =this->fixquality;
  this->unlock();
  return value;
}

int AutoPilot::getSatellites() {
  this->lock();
  int value =this->satellites;
  this->unlock();
  return value;
}

int AutoPilot::getMode() {
  this->lock();
  int value =this->mode;
  this->unlock();
  return value;
}

void AutoPilot::setMode(int new_mode) {
  this->lock();
  this->localCommandTime = millis();
  if ((new_mode == 1) || (new_mode == 2 && this->waypoint_set)) {
    this->mode = new_mode;
    if (this->mode == 1) {
      this->heading_desired = this->heading;
      this->bearing = this->heading_desired;
    }
    this->modeChanged = true;
    this->destinationChanged = true;
  }
  this->unlock();
}

bool AutoPilot::isNavigationEnabled() {
  this->lock();
  bool value =this->navigation_enabled;
  this->unlock();
  return value;
}

void AutoPilot::setNavigationEnabled(bool enable) {
  this->lock();
  this->localCommandTime = millis();
  if (this->navigation_enabled == false && enable == true) {
    // if we are re-enabling and current mode is compass we should stat to navigate to current heading to previous one.
    if (this->mode == 1) {
      this->heading_desired = this->heading;
      this->bearing = this->heading_desired;
      this->bearing_correction = 0;
    }
  }
  this->modeChanged = true;
  this->destinationChanged = true;
  this->navigation_enabled = enable;
  this->unlock();  
}

bool AutoPilot::isWaypointSet() {
  this->lock();
  bool value =this->waypoint_set;
  this->unlock();
  return value;
}

float AutoPilot::getWaypointLat() {
  this->lock();
  float value =this->waypoint_lat;
  this->unlock();
  return value;
}

float AutoPilot::getWaypointLon() {
  this->lock();
  float value =this->waypoint_lon;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingDesired() {
  this->lock();
  float value =this->heading_desired;
  this->unlock();
  return value;
}

float AutoPilot::getHeading() {
  this->lock();
  float value =this->heading;
  this->unlock();
  return value;
}

void AutoPilot::adjustHeadingDesired(float change) {
  this->lock();
  this->localCommandTime = millis();
  if (this->mode > 0) {
    if (this->mode == 2) {
      this->heading_desired = this->heading;
      this->bearing = this->heading_desired;
      this->modeChanged = true;
    }
    this->mode = 1;
    this->heading_desired = normalizeDegrees(this->heading_desired + change);
    this->bearing = this->heading_desired;
    this->destinationChanged = true;
    this->bearing_correction = this->getCourseCorrection(this->bearing, this->heading);

  }
  this->unlock();
}

float AutoPilot::getPitch() {
  this->lock();
  float value =this->pitch;
  this->unlock();
  return value;
}

float AutoPilot::getRoll() {
  this->lock();
  float value =this->roll;
  this->unlock();
  return value;
}

int AutoPilot::getStabilityClassification() {
  this->lock();
  int value =this->stability_classification;
  this->unlock();
  return value;
}

float AutoPilot::getBearing() {
  this->lock();
  float value =this->bearing;
  this->unlock();
  return value;
}

float AutoPilot::getBearingCorrection() {
  this->lock();
  float value =this->bearing_correction;
  this->unlock();
  return value;
}

float AutoPilot::getSpeed() {
  this->lock();
  float value =this->speed;
  this->unlock();
  return value;
}

float AutoPilot::getDistance() {
  this->lock();
  float value =this->distance;
  this->unlock();
  return value;
}

float AutoPilot::getCourse() {
  this->lock();
  float value =this->course;
  this->unlock();
  return value;
}

float AutoPilot::getLocationLat() {
  this->lock();
  float value =this->location_lat;
  this->unlock();
  return value;
}

float AutoPilot::getLocationLon() {
  this->lock();
  float value =this->location_lon;
  this->unlock();
  return value;
}

bool AutoPilot::hasDestinationChanged() {
  this->lock();
  bool value = this->destinationChanged;
  this->destinationChanged = false;
  this->unlock();
  return value;
}

bool AutoPilot::hasModeChanged() {
  this->lock();
  bool value = this->modeChanged;
  this->modeChanged = false;
  this->unlock();
  return value;
}

float AutoPilot::getBatteryVoltage() {
  this->lock();
  float value =this->battery_voltage;
  this->unlock();
  return value;
}

void AutoPilot::setBatteryVoltage(float voltage) {
  this->lock();
  if (voltage > 0.2) {
    if (this->battery_voltage_average_size < BATTERY_VOLTS_AVERAGE_MAX_SIZE) {
      this->battery_voltage_average_size += 1;
    }
    this->battery_voltage = this->battery_voltage + ((voltage - this->battery_voltage) / this->battery_voltage_average_size);
  } else {
    this->battery_voltage = 0.0;
    this->battery_voltage_average_size = 0;
  }
  this->unlock();
}

float AutoPilot::getInputVoltage() {
  this->lock();
  float value =this->input_voltage;
  this->unlock();
  return value;
}

void AutoPilot::setInputVoltage(float voltage) {
  this->lock();
  if (voltage > 0.2) {
    if (this->input_voltage_average_size < INPUT_VOLTS_AVERAGE_MAX_SIZE) {
      this->input_voltage_average_size += 1;
    }
    this->input_voltage = this->input_voltage + ((voltage - this->input_voltage) / this->input_voltage_average_size);
  } else {
    this->input_voltage = 0.0;
    this->input_voltage_average_size = 0;
  }
  this->unlock();
}

bool AutoPilot::getReset() {
  this->lock();
  bool value =this->reset;
  this->unlock();
  return value;
}

void AutoPilot::setReset(bool value) {
  this->lock();
  this->reset = value;
  this->unlock();
}

bool AutoPilot::isConnected() {
  this->lock();
  bool value =this->connected;
  this->unlock();
  return value;
}

void AutoPilot::setConnected(bool connected) {
  this->lock();
  if (this->connected != connected) {
    this->modeChanged = true;
    this->destinationChanged = true;
  }
  this->connected = connected;
  this->unlock();
}

unsigned long AutoPilot::getTackRequested() {
  this->lock();
  unsigned long value =this->tackRequested;
  this->unlock();
  return value;
}

void AutoPilot::setTackRequested(unsigned long time) {
  this->lock();
  this->tackRequested = time;
  this->modeChanged = true;
  this->unlock();
}

void AutoPilot::cancelTackRequested() {
  this->lock();
  this->tackRequested = 0;
  this->modeChanged = true;
  this->unlock();
}

bool AutoPilot::isTackRequested() {
  this->lock();
  bool value =(this->tackRequested > 0);
  this->unlock();
  return value;
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

// Advance to the character after the next comma. Returns NULL if there is no
// further field (no more commas, or the input is already NULL). All field reads
// below guard with isEmpty(), and isEmpty(NULL) == true, so a short/garbled
// sentence simply leaves the remaining fields at their default (0) values
// instead of dereferencing an invalid pointer.
static char *advance_field(char *p) {
  if (p == NULL) {
    return NULL;
  }
  char *comma = strchr(p, ',');
  return (comma == NULL) ? NULL : comma + 1;
}

void AutoPilot::parseAPDAT(char *sentence) {
  char *p = sentence;  // Pointer to move through the sentence -- good parsers are non-destructive

  // The parser runs on the command task while the display task reads these
  // fields through the locked getters; hold the lock so the whole update is
  // atomic (and so the modeChanged/destinationChanged flags can't be lost).
  this->lock();

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->year = atoi(p);
  } else {
    this->year = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->month = atoi(p);
  } else {
    this->month = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->day = atoi(p);
  } else {
    this->day = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->hour = atoi(p);
  } else {
    this->hour = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->minute = atoi(p);
  } else {
    this->minute = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
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

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->fixquality = atoi(p);
  } else {
    this->fixquality = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->satellites = atoi(p);
  } else {
    this->satellites = 0;
  }

  bool suppressLocalFields = (millis() - this->localCommandTime) < LOCAL_COMMAND_SUPPRESS_MS;

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!suppressLocalFields) {
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
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!suppressLocalFields) {
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
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
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

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  float currentWaypointLat = this->waypoint_lat;
  if (!isEmpty(p)) {
    this->waypoint_lat = atof(p);
  } else {
    this->waypoint_lat = 0.0;
  }
  if ((currentWaypointLat != this->waypoint_lat) && (this->mode == 2)) {
    this->destinationChanged = true;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  float currentWaypointLon = this->waypoint_lon;
  if (!isEmpty(p)) {
    this->waypoint_lon = atof(p);
  } else {
    this->waypoint_lon = 0.0;
  }
  if ((currentWaypointLon != this->waypoint_lon) && (this->mode == 2)) {
    this->destinationChanged = true;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!suppressLocalFields) {
    float currentHeadingDesired = this->heading_desired;
    if (!isEmpty(p)) {
      this->heading_desired = atof(p);
    } else {
      this->heading_desired = 0.0;
    }
    if ((currentHeadingDesired != this->heading_desired) && (this->mode == 1)) {
      this->destinationChanged = true;
    }
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->heading = atof(p);
  } else {
    this->heading = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->pitch = atof(p);
  } else {
    this->pitch = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->roll = atof(p);
  } else {
    this->roll = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->stability_classification = atoi(p);
  } else {
    this->stability_classification = 0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!suppressLocalFields) {
    if (!isEmpty(p)) {
      this->bearing = atof(p);
    } else {
      this->bearing = 0.0;
    }
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!suppressLocalFields) {
    if (!isEmpty(p)) {
      this->bearing_correction = atof(p);
    } else {
      this->bearing_correction = 0.0;
    }
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->speed = atof(p);
  } else {
    this->speed = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->distance = atof(p);
  } else {
    this->distance = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->course = atof(p);
  } else {
    this->course = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->location_lat = atof(p);
  } else {
    this->location_lat = 0.0;
  }

  p = advance_field(p);  // Advance to the next field; NULL if none remain.
  if (!isEmpty(p)) {
    this->location_lon = atof(p);
  } else {
    this->location_lon = 0.0;
  }

  this->unlock();
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
  if (pStart != NULL && ',' != *pStart && '*' != *pStart)
    return false;
  else
    return true;
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

float AutoPilot::normalizeDegrees(float degrees) {
  // Map any angle (positive or negative, any magnitude) to a compass bearing in
  // the range [0, 360).  e.g. -90 -> 270, 450 -> 90, -450 -> 270, 360 -> 0.
  degrees = fmodf(degrees, 360.0f);
  if (degrees < 0.0f) {
    degrees += 360.0f;
  }
  return degrees;
}

void AutoPilot::lock() {
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

void AutoPilot::unlock() {
  xSemaphoreGiveRecursive(mutex);
}