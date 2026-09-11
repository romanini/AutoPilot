#include <Arduino.h>
#include "AutoPilot.h"

// ~APDAT field positions, counting from 1 for the first field after "APDAT,".
// The sentence is positional, so these are the whole contract with
// controller/publish.ino - if a field is ever inserted rather than appended
// there (it should not be; the convention is append-only), these move.
//
// Named only for the fields this unit actually renders. Everything else is
// walked past by index without a constant, because naming a field we do not use
// would imply we track it.
#define APDAT_FIELD_COUNT       39
#define FIELD_FIX                6
#define FIELD_HEADING           15
#define FIELD_SPEED             21
#define FIELD_WIND_ANGLE        32
#define FIELD_WIND_SPEED        33
#define FIELD_WIND_OK           34
#define FIELD_TRUE_WIND_ANGLE   35
#define FIELD_TRUE_WIND_SPEED   36
#define FIELD_TRUE_WIND_OK      37
#define FIELD_AIR_TEMPERATURE   38
#define FIELD_AIR_TEMPERATURE_OK 39

AutoPilot::AutoPilot(SerialType *ser) {
  serial = ser;

  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == NULL) {
    serial->println("mutex creation failed");
    while (1)
      ;
  }
  this->init();
}

AutoPilot::~AutoPilot() {
  vSemaphoreDelete(mutex);
}

// Also the "we lost the controller" reset, called from check_subscription() on
// a receive timeout: every value goes back to its no-data state so the screen
// cannot keep showing a stale wind angle from ten minutes ago. Lockable so that
// reset (display task) is safe against a parse in flight (AsyncUDP task).
void AutoPilot::init() {
  this->lock();
  fix = false;
  heading = 0.0;
  speed = 0.0;
  wind_angle = 0.0;
  wind_speed = 0.0;
  wind_ok = false;
  true_wind_angle = 0.0;
  true_wind_speed = 0.0;
  true_wind_ok = false;
  air_temperature = 0.0;
  air_temperature_ok = false;
  connected = false;
  this->unlock();
}

void AutoPilot::lock() {
  xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
}

void AutoPilot::unlock() {
  xSemaphoreGiveRecursive(mutex);
}

bool AutoPilot::isEmpty(char *p) {
  return (p == NULL || *p == ',' || *p == '\0');
}

// Advance to the character after the next comma. Returns NULL if there is no
// further field. Every read below guards with isEmpty(), and isEmpty(NULL) is
// true, so a short or garbled sentence leaves the remaining fields at their
// defaults instead of dereferencing an invalid pointer.
static char *advance_field(char *p) {
  if (p == NULL) {
    return NULL;
  }
  char *comma = strchr(p, ',');
  return (comma == NULL) ? NULL : comma + 1;
}

void AutoPilot::parse(char *sentence) {
  if (strncmp(sentence, APDAT, 6) == 0) {
    parseAPDAT(sentence);
    setConnected(true);
  } else if (strncmp(sentence, RESET, 6) == 0) {
    // The controller announces its own restart. Nothing to do here beyond
    // noting that it is talking - the next ~APDAT repopulates everything.
    setConnected(true);
  } else if (strncmp(sentence, "APRX,", 5) == 0) {
    // Garmin NMEA relay frames share the telemetry broadcast port and arrive
    // continuously while the Garmin navigates. Drop them silently - they must
    // never reach the print path below.
  } else {
    // Guarded like the DEBUG_* macros: this runs on the AsyncUDP task, and a
    // CDC write during a physical USB detach can park the task on an internal
    // USB lock (see the note at the top of wind-display.ino).
    if (*serial) serial->println("unknown sentence");
  }
}

// Walks all 39 fields by position and keeps the ten this unit draws.
//
// The switch runs inside one lock so the whole update is atomic: the display
// task must never catch an apparent angle from this packet next to a true angle
// from the last one.
//
// Trailing fields are tolerated as absent (the loop breaks when it runs out),
// which is the project-wide convention - a controller running older firmware
// simply leaves wind_ok/true_wind_ok false, and the screen says "no wind data"
// rather than drawing a fabricated calm-dead-ahead.
void AutoPilot::parseAPDAT(char *sentence) {
  char *p = sentence;  // at the 'A' of "APDAT"; non-destructive walk

  this->lock();

  for (int index = 1; index <= APDAT_FIELD_COUNT; index++) {
    p = advance_field(p);
    if (p == NULL) {
      break;  // short frame - everything past here keeps its previous value
    }
    if (isEmpty(p)) {
      continue;  // empty field - same treatment
    }

    switch (index) {
      case FIELD_FIX:                 this->fix = atoi(p) > 0;              break;
      case FIELD_HEADING:             this->heading = atof(p);              break;
      case FIELD_SPEED:               this->speed = atof(p);                break;
      case FIELD_WIND_ANGLE:          this->wind_angle = atof(p);           break;
      case FIELD_WIND_SPEED:          this->wind_speed = atof(p);           break;
      case FIELD_WIND_OK:             this->wind_ok = atoi(p) > 0;          break;
      case FIELD_TRUE_WIND_ANGLE:     this->true_wind_angle = atof(p);      break;
      case FIELD_TRUE_WIND_SPEED:     this->true_wind_speed = atof(p);      break;
      case FIELD_TRUE_WIND_OK:        this->true_wind_ok = atoi(p) > 0;     break;
      case FIELD_AIR_TEMPERATURE:     this->air_temperature = atof(p);      break;
      case FIELD_AIR_TEMPERATURE_OK:  this->air_temperature_ok = atoi(p) > 0; break;
      default: break;  // walked for position only - see the header comment
    }
  }

  this->unlock();
}

bool AutoPilot::hasFix() {
  this->lock();
  bool value = this->fix;
  this->unlock();
  return value;
}

float AutoPilot::getHeading() {
  this->lock();
  float value = this->heading;
  this->unlock();
  return value;
}

float AutoPilot::getSpeed() {
  this->lock();
  float value = this->speed;
  this->unlock();
  return value;
}

float AutoPilot::getWindAngle() {
  this->lock();
  float value = this->wind_angle;
  this->unlock();
  return value;
}

float AutoPilot::getWindSpeed() {
  this->lock();
  float value = this->wind_speed;
  this->unlock();
  return value;
}

bool AutoPilot::isWindOk() {
  this->lock();
  bool value = this->wind_ok;
  this->unlock();
  return value;
}

float AutoPilot::getTrueWindAngle() {
  this->lock();
  float value = this->true_wind_angle;
  this->unlock();
  return value;
}

float AutoPilot::getTrueWindSpeed() {
  this->lock();
  float value = this->true_wind_speed;
  this->unlock();
  return value;
}

bool AutoPilot::isTrueWindOk() {
  this->lock();
  bool value = this->true_wind_ok;
  this->unlock();
  return value;
}

float AutoPilot::getAirTemperature() {
  this->lock();
  float value = this->air_temperature;
  this->unlock();
  return value;
}

bool AutoPilot::isAirTemperatureOk() {
  this->lock();
  bool value = this->air_temperature_ok;
  this->unlock();
  return value;
}

bool AutoPilot::isConnected() {
  this->lock();
  bool value = this->connected;
  this->unlock();
  return value;
}

void AutoPilot::setConnected(bool connected) {
  this->lock();
  this->connected = connected;
  this->unlock();
}
