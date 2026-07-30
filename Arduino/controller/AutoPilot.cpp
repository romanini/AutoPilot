#include <Arduino.h>
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
// A stationary GPS still reports a jittery non-zero speed (measured up to ~0.6 kn).
// Snap anything below this to zero. Keep this >= the PMTK386 static-nav threshold
// set in setup_gps() (0.2 m/s ~= 0.39 kn) so it acts as a true backstop.
#define GPS_SPEED_DEADBAND_KNOTS 0.8

// Hysteresis latch for trusting raw COG enough to fold it into the damped
// average (Doppler COG quality scales with speed; PMTK386 freezes course
// entirely near 0). Same values/reasoning as the old COG_FALLBACK_ENABLED
// latch in controller.ino, which this supersedes.
#define COG_TRUST_ENTER_KNOTS 1.0f
#define COG_TRUST_EXIT_KNOTS  0.8f
// Exponential-smoothing coefficient for the vector-averaged COG. setLocation()
// runs roughly once per GPS fix (~1 Hz), so alpha ~= dt/tau for a ~10s time
// constant.
#define COG_DAMPING_ALPHA 0.1

AutoPilot::AutoPilot(SerialType* ser) {
  serial = ser;

  mutex = xSemaphoreCreateRecursiveMutex();
  if (mutex == NULL) {
    serial->println("mutex creation failed");
    while (1)
      ;
  }

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

  navigation_enabled = false;
  mode = 1;
  compass_fallback = false;
  heading_desired = 0.0;
  bearing = 0.0;
  bearing_correction = 0.0;
  nav_source = 0;

  heading_command = 0.0;
  cog_usable = false;
  cog_damped_initialized = false;
  cog_sin_avg = 0.0;
  cog_cos_avg = 0.0;
  cog_damped = 0.0;

  heading = 0.0;
  pitch = 0.0;
  roll = 0.0;
  stability_classification = 0;
  
  start_motor = 0;
  motor_started = false;

  rudder_angle = 0.0;
  rudder_magnet_ok = false;

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

  autoTuneState = 0;
  autoTuneReadyAt = 0;
}

AutoPilot::~AutoPilot() {
  if (mutex != NULL) {
    vSemaphoreDelete(mutex);
  }
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
  if (!fix) {
    // Lost the GPS fix. If we were steering to a waypoint, don't disengage -
    // a momentary fix dropout would otherwise silently stop the autopilot.
    // Instead hold the current heading (compass mode) with navigation still
    // engaged, and remember that we did this so we can resume waypoint nav once
    // the fix returns. Guarded by mode == 2 so repeated no-fix cycles are no-ops.
    if (this->mode == 2) {
      this->compass_fallback = true;
      this->mode = 1;
      this->heading_desired = this->heading;
      this->bearing = this->heading_desired;
      this->bearing_correction = getCourseCorrection(this->bearing, this->heading);
      this->modeChanged = true;
      this->destinationChanged = true;
    }
  } else if (this->compass_fallback) {
    // Fix restored after an automatic compass-hold fallback. Resume waypoint
    // navigation only if the operator hasn't taken manual control in the
    // meantime (which clears compass_fallback) and we still have a waypoint and
    // navigation is on.
    if (this->waypoint_set && this->navigation_enabled) {
      this->mode = 2;
      this->modeChanged = true;
      this->destinationChanged = true;
    }
    this->compass_fallback = false;
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

int AutoPilot::setMode(int new_mode) {
  this->lock();
  int retval = -1;
  if ((new_mode == 1) || (new_mode == 2 && this->waypoint_set)) {
    // Operator chose a mode explicitly - cancel any pending auto-resume to
    // waypoint nav from a GPS fix-loss fallback.
    this->compass_fallback = false;
    this->mode = new_mode;
    if (this->mode == 1) {
      this->heading_desired = this->heading;
      this->bearing = this->heading_desired;
    } else if (this->mode == 2 && this->fix) {
      // Entering waypoint nav: refresh bearing-to-mark now rather than waiting
      // for the next GPS fix, and seed heading_command so the wheel doesn't
      // lurch - it starts at the current compass heading, corrected for
      // whatever track error is already known.
      this->bearing = this->getBearing(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
      this->seedHeadingCommand();
    }
    this->modeChanged = true;
    this->destinationChanged = true;
    retval = 0;
  }
  this->unlock();
  return retval;
}

int AutoPilot::getNavSource() {
  this->lock();
  int value = this->nav_source;
  this->unlock();
  return value;
}

// Record which source the selector is steering by (0=NONE, 1=GARMIN, 2=OPENCPN).
// Published in APDAT so the displays/plugin can show "who's steering"; it does
// NOT gate steering (that's navigation_enabled + mode).
void AutoPilot::setNavSource(int source) {
  this->lock();
  this->nav_source = source;
  this->unlock();
}

bool AutoPilot::isNavigationEndabled() {
  this->lock();
  bool value = this->navigation_enabled;
  this->unlock();
  return value;
}

void AutoPilot::setNavigationEnabled(bool enable) {
  this->lock();
  // Operator toggled navigation explicitly - cancel any pending auto-resume to
  // waypoint nav from a GPS fix-loss fallback.
  this->compass_fallback = false;
  // Relay auto-tune (autotune.ino) only ever runs while navigation is disabled.
  // If something re-enables navigation out from under an armed/running tune
  // (telnet, a stale display, ...) cancel it rather than leaving stale state -
  // control_task stops driving the relay the moment autoTuneState != 2.
  if (enable && this->autoTuneState != 0) {
    this->autoTuneState = 0;
    this->autoTuneReadyAt = 0;
  }
  if (this->navigation_enabled == false && enable == true) {
    // if we are re-enabling and current mode is compass we should stat to navigate to current heading to previous one.
    if (this->mode == 1) {
      this->heading_desired = this->heading;
      this->bearing = this->heading_desired;
      this->bearing_correction = 0;
    }
    this->destinationChanged = true;    
  } 
  this->navigation_enabled = enable;
  this->unlock();  
}

float AutoPilot::getHeadingDesired() {
  this->lock();
  float value = this->heading_desired;
  this->unlock();
  return value;
}

void AutoPilot::adjustHeadingDesired(float change) {
  this->lock();
  // Operator is steering by hand - cancel any pending auto-resume to waypoint
  // nav from a GPS fix-loss fallback.
  this->compass_fallback = false;
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
    this->unlock();
    return;
  }
  this->heading = heading;
  if (this->mode == 1) {
    this->bearing_correction = this->getCourseCorrection(this->bearing, this->heading);
  }
  this->unlock();
}

float AutoPilot::getPitch() {
  this->lock();
  float value = this->pitch;
  this->unlock();
  return value;
}

void AutoPilot::setPitch(float pitch) {
  this->lock();
  this->pitch = pitch;
  this->unlock();
}

float AutoPilot::getRoll() {
  this->lock();
  float value = this->roll;
  this->unlock();
  return value;
}

void AutoPilot::setRoll(float roll) {
  this->lock();
  this->roll = roll;
  this->unlock();
}

float AutoPilot::getRudderAngle() {
  this->lock();
  float value = this->rudder_angle;
  this->unlock();
  return value;
}

bool AutoPilot::isRudderMagnetOk() {
  this->lock();
  bool value = this->rudder_magnet_ok;
  this->unlock();
  return value;
}

void AutoPilot::setRudderAngle(float angle, bool magnet_ok) {
  this->lock();
  this->rudder_angle = angle;
  this->rudder_magnet_ok = magnet_ok;
  this->unlock();
}

int AutoPilot::getStabilityClassification() {
  this->lock();
  float value = this->stability_classification;
  this->unlock();
  return value;
}

void AutoPilot::setStabilityClassification(int value) {
  this->lock();
  this->stability_classification = value;
  this->unlock();
}

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
    if (this->fix) {
      // A new destination mid-leg (e.g. the next route waypoint) invalidates
      // the old bearing - refresh it and reseed heading_command so a sharp
      // turn on the chart doesn't wait out several trim ticks to catch up.
      this->bearing = this->getBearing(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
      this->seedHeadingCommand();
    }
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

void AutoPilot::setLocation(float lat, float lon, float course) {
  this->lock();
  this->location_lat = lat;
  this->location_lon = lon;
  this->course = course;

  // Maintain the vector-averaged (sin/cos) damped COG. Trust hysteresis: only
  // fold a sample in while speed is comfortably above the GPS's static-nav
  // threshold - Doppler COG is noisy at low speed and PMTK386 freezes it near
  // zero anyway. setSpeed() always runs before setLocation() (see gps.ino), so
  // this->speed already reflects the current fix.
  if (this->speed >= COG_TRUST_ENTER_KNOTS) {
    this->cog_usable = true;
  } else if (this->speed < COG_TRUST_EXIT_KNOTS) {
    this->cog_usable = false;
  }
  if (this->cog_usable) {
    float rad = this->toRadians(course);
    double s = sin(rad);
    double c = cos(rad);
    if (!this->cog_damped_initialized) {
      this->cog_sin_avg = s;
      this->cog_cos_avg = c;
      this->cog_damped_initialized = true;
    } else {
      this->cog_sin_avg += COG_DAMPING_ALPHA * (s - this->cog_sin_avg);
      this->cog_cos_avg += COG_DAMPING_ALPHA * (c - this->cog_cos_avg);
    }
    this->cog_damped = this->normalizeDegrees(this->toDegrees(atan2(this->cog_sin_avg, this->cog_cos_avg)));
  }

  if (this->waypoint_set) {
    this->distance = this->getDistance(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
    if (this->mode == 2) {
      this->bearing = this->getBearing(this->location_lat, this->location_lon, this->waypoint_lat, this->waypoint_lon);
      // Now "how far is my GPS track trending from the mark" (what
      // gpstracktrim.ino is correcting), not a per-fix noisy snapshot -
      // steady and meaningful once cog_damped is trusted; 0 (nothing to
      // report yet) until then.
      this->bearing_correction = (this->cog_usable && this->cog_damped_initialized)
          ? this->getCourseCorrection(this->bearing, this->cog_damped)
          : 0.0f;
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
  // Backstop the GPS static-nav threshold: zero out residual speed noise that
  // slips through (or the whole noise floor, if the GPS ignored/lost PMTK386).
  // Speed is display/telemetry only, so this never affects steering.
  if (speed < GPS_SPEED_DEADBAND_KNOTS) {
    speed = 0.0;
  }
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
  // Worst case "12/31/99 23:59" = 14 chars + NUL; 13 was an overflow waiting
  // for this function to be re-enabled. Same sizing as telnet.ino's copy.
  char dateTimeString[16];
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

  return normalizeDegrees(bearing);
}

// Not locked - only ever called from setMode()/setWaypoint() while the
// recursive mutex is already held.
void AutoPilot::seedHeadingCommand() {
  if (this->cog_usable && this->cog_damped_initialized) {
    this->heading_command = this->normalizeDegrees(this->heading + this->getCourseCorrection(this->bearing, this->cog_damped));
  } else {
    this->heading_command = this->heading;
  }
}

bool AutoPilot::isDampedCourseValid() {
  this->lock();
  bool value = this->cog_usable && this->cog_damped_initialized;
  this->unlock();
  return value;
}

float AutoPilot::getDampedCourse() {
  this->lock();
  float value = this->cog_damped;
  this->unlock();
  return value;
}

float AutoPilot::getHeadingCommand() {
  this->lock();
  float value = this->heading_command;
  this->unlock();
  return value;
}

// Nudge heading_command toward whatever GPS-frame target_course the caller
// selected (plain bearing-to-mark, or the XTE-blended course) by a clamped
// fraction of the error between it and the damped COG. This is the only
// place gpstracktrim.ino touches AutoPilot state - it never reads/writes
// cog_damped or heading_command directly, so the angle-wrap math stays
// inside this class alongside everything else that manipulates it.
void AutoPilot::applyHeadingCommandTrim(float target_course, float gain, float clamp_deg) {
  this->lock();
  float error = this->getCourseCorrection(target_course, this->cog_damped);
  float adjust = gain * error;
  if (adjust > clamp_deg) {
    adjust = clamp_deg;
  } else if (adjust < -clamp_deg) {
    adjust = -clamp_deg;
  }
  this->heading_command = this->normalizeDegrees(this->heading_command + adjust);
  this->unlock();
}

int AutoPilot::getAutoTuneState() {
  this->lock();
  int value = this->autoTuneState;
  this->unlock();
  return value;
}

unsigned long AutoPilot::getAutoTuneReadyAt() {
  this->lock();
  unsigned long value = this->autoTuneReadyAt;
  this->unlock();
  return value;
}

// Arm relay auto-tune ("ready"). Callers (subscribe.ino, telnet.ino) are
// expected to have already checked isNavigationEndabled() == false and
// autoTuneState == 0 - this just records the transition.
void AutoPilot::armAutoTune() {
  this->lock();
  this->autoTuneState = 1;
  this->autoTuneReadyAt = millis();
  this->modeChanged = true;
  this->destinationChanged = true;
  this->unlock();
}

// Move from "ready" to actually running the relay. autotune.ino resets its own
// (module-local) oscillation-measurement state around this call.
void AutoPilot::startAutoTune() {
  this->lock();
  this->autoTuneState = 2;
  this->modeChanged = true;
  this->destinationChanged = true;
  this->unlock();
}

// Cancel from any state (ready-timeout, operator abort, or a completed tune).
void AutoPilot::cancelAutoTune() {
  this->lock();
  this->autoTuneState = 0;
  this->autoTuneReadyAt = 0;
  this->modeChanged = true;
  this->destinationChanged = true;
  this->unlock();
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