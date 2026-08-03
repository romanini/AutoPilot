#include "Wind.h"

#include <Arduino.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Calculation tuning. Everything here is ported from the original Yachta
// firmware's Calculation.h / Definitions.h; deviations are called out at the
// point where they happen.
// ---------------------------------------------------------------------------

// Wind speed calibration, applied to m/s: speed = raw * slope + offset.
// The original exposes these on its settings web page; we have no web page, so
// they are compile-time. Defaults are the original's (identity).
#define WIND_CAL_SLOPE 1.0f
#define WIND_CAL_OFFSET 0.0f

// Anything above this many revolutions per second is nonsense - the original
// uses it to swallow the huge first value produced right after a restart, when
// the "previous pulse" timestamp is meaningless.
#define SPEED_HZ_SANITY_LIMIT 100.0f

// Rate limit on apparent wind angle, in degrees per second.
//
// The original clamps the change between two consecutive samples to 45 deg,
// and its calculation timer runs at 500 ms - i.e. 90 deg/s. Expressing it as a
// rate rather than a per-sample step is a deliberate change: this port
// calculates far more often than twice a second (see CALCULATE_INTERVAL_MS in
// wind.ino), and a fixed 45 deg per sample at 10 Hz would be 450 deg/s, which
// is no limit at all.
//
// The wrap handling is also different, and this one is a bug fix rather than a
// re-scaling. The original disables the limiter whenever the angle is within
// 45 deg of the bow, because its subtraction can't tell a small movement
// across 0/360 from a 350 deg jump. Computing the *signed shortest* difference
// (degreesDelta below) handles the wrap correctly everywhere, so the limiter
// stays armed in the sector that matters most for sailing - close-hauled, and
// the sector a wind-vane steering mode would eventually steer in.
#define MAX_DIRECTION_SLEW_DPS 90.0f

// The wind is treated as "astern" within this many degrees either side of 180.
// Feeds the downwind speed component, which is what the original's VPW
// sentence carries (actconf.downWindRange, default 50).
#define DOWNWIND_RANGE_DEG 50.0f

Wind::Wind(SerialType* ser) {
  mutex = xSemaphoreCreateRecursiveMutex();
  serial = ser;

  raw_direction = 0.0;
  vane_magnitude = 0;
  vane_ok = false;
  rotation_period_ms = 0.0;
  rotation_valid = false;
  temperature = 0.0;
  temperature_ok = false;

  direction = 0.0;
  direction_side = 0.0;
  direction_seeded = false;
  speed_hz = 0.0;
  speed_mps = 0.0;
  speed_kn = 0.0;
  speed_kph = 0.0;
  speed_bft = 0;
  downwind_mps = 0.0;
  downwind_kn = 0.0;
  last_calculation = 0;
}

Wind::~Wind() {
  if (mutex != NULL) {
    vSemaphoreDelete(mutex);
  }
}

void Wind::lock() {
  if (mutex != NULL) {
    xSemaphoreTakeRecursive(mutex, portMAX_DELAY);
  }
}

void Wind::unlock() {
  if (mutex != NULL) {
    xSemaphoreGiveRecursive(mutex);
  }
}

// Round-half-away-from-zero, matching roundFloat2Int() in the original.
int Wind::roundFloat2Int(float x) {
  if (x > 0) return (int)(x + 0.5);
  return (int)(x - 0.5);
}

// Fold any angle into 0..360.
float Wind::normalizeDegrees(float degrees) {
  degrees = fmodf(degrees, 360.0f);
  if (degrees < 0) {
    degrees += 360.0f;
  }
  return degrees;
}

// Signed shortest angular distance from `from` to `to`, in -180..180. This is
// what makes the rate limiter behave across the 0/360 seam - see the comment
// on MAX_DIRECTION_SLEW_DPS.
float Wind::degreesDelta(float from, float to) {
  float delta = fmodf(to - from + 540.0f, 360.0f) - 180.0f;
  return delta;
}

// ---------------------------------------------------------------------------
// Inputs
// ---------------------------------------------------------------------------

void Wind::setVane(float degrees, uint16_t magnitude, bool ok) {
  lock();
  raw_direction = degrees;
  vane_magnitude = magnitude;
  vane_ok = ok;
  unlock();
}

void Wind::setRotationPeriod(float period_ms, bool valid) {
  lock();
  rotation_period_ms = period_ms;
  rotation_valid = valid;
  unlock();
}

void Wind::setTemperature(float celsius, bool ok) {
  lock();
  temperature = celsius;
  temperature_ok = ok;
  unlock();
}

// ---------------------------------------------------------------------------
// The maths
// ---------------------------------------------------------------------------

void Wind::calculate() {
  lock();

  unsigned long now = millis();
  float elapsed_s = (last_calculation == 0) ? 0.0f : (now - last_calculation) / 1000.0f;
  last_calculation = now;

  // ---- apparent wind angle ------------------------------------------------
  // A vane reading is only used when the AS5600 actually sees its magnet. The
  // original substitutes 0 when the sensor is unreadable, which reads on the
  // wire as a perfectly good "wind dead ahead"; holding the last angle and
  // letting the vane_ok flag travel alongside it is safer for a consumer that
  // might one day steer on this.
  if (vane_ok) {
    float candidate = normalizeDegrees(raw_direction);
    if (!direction_seeded || elapsed_s <= 0.0f) {
      // First ever sample (or a clock that hasn't advanced): take it as-is.
      // Rate limiting from a standing start would otherwise walk the angle up
      // from 0 over several seconds after every boot.
      direction = candidate;
      direction_seeded = true;
    } else {
      float delta = degreesDelta(direction, candidate);
      float max_step = MAX_DIRECTION_SLEW_DPS * elapsed_s;
      if (delta > max_step) {
        delta = max_step;
      } else if (delta < -max_step) {
        delta = -max_step;
      }
      direction = normalizeDegrees(direction + delta);
    }
  }

  // Same angle expressed as 0..180 off either bow, the form the original's VWR
  // sentence uses (paired with an L/R side indicator). Not currently sent on
  // ~APWND - it is derivable from direction - but kept here so the controller
  // can emit NMEA VWR later without re-deriving it.
  direction_side = (direction <= 180.0f) ? direction : 360.0f - direction;

  // ---- wind speed ---------------------------------------------------------
  // rotation_period_ms is the interval between reed-switch pulses; there are
  // ANEMOMETER_PULSES_PER_REV of those per turn, hence the extra division.
  // Periods that have hit the clamp are not trusted, so speed_hz simply keeps
  // its previous value until either a faster pulse arrives or the zero-wind
  // timeout in anemometer.ino clears rotation_valid.
  if (rotation_valid && rotation_period_ms > 0.0f && rotation_period_ms < ANEMOMETER_PERIOD_LIMIT_MS) {
    speed_hz = 1000.0f / rotation_period_ms / ANEMOMETER_PULSES_PER_REV;
  }

  if (speed_hz > SPEED_HZ_SANITY_LIMIT) {
    speed_hz = 0.0f;
  }
  if (!rotation_valid) {
    speed_hz = 0.0f;  // anemometer has stopped
  }

  // v[m/s] = (2 * pi * n[Hz] * r[m]) / lambda
  speed_mps = (2.0f * (float)PI * speed_hz * ANEMOMETER_RADIUS_M) / ANEMOMETER_LAMBDA;
  speed_mps = speed_mps * WIND_CAL_SLOPE + WIND_CAL_OFFSET;
  if (speed_mps < 0.0f) {
    speed_mps = 0.0f;
  }

  speed_kph = speed_mps * 3.6f;
  speed_kn = speed_mps * 1.94384f;

  // v[bft] = 0.0000222*v^3 - 0.0034132*v^2 + 0.2981666*v + 0.1467082, v in knots
  float term3 = 0.0000222f * speed_kn * speed_kn * speed_kn;
  float term2 = 0.0034132f * speed_kn * speed_kn;
  float term1 = 0.2981666f * speed_kn;
  speed_bft = roundFloat2Int(term3 - term2 + term1 + 0.1467082f);
  if (speed_bft > 12) {
    speed_bft = 12;
  }
  if (speed_bft < 0) {
    speed_bft = 0;
  }

  // ---- downwind component -------------------------------------------------
  if (direction >= (180.0f - DOWNWIND_RANGE_DEG) && direction <= (180.0f + DOWNWIND_RANGE_DEG)) {
    downwind_mps = speed_mps;
    downwind_kn = speed_kn;
  } else {
    downwind_mps = 0.0f;
    downwind_kn = 0.0f;
  }

  unlock();
}

// ---------------------------------------------------------------------------
// Getters
// ---------------------------------------------------------------------------

float Wind::getDirection() {
  lock();
  float value = direction;
  unlock();
  return value;
}

float Wind::getDirectionSide() {
  lock();
  float value = direction_side;
  unlock();
  return value;
}

float Wind::getRawDirection() {
  lock();
  float value = raw_direction;
  unlock();
  return value;
}

uint16_t Wind::getVaneMagnitude() {
  lock();
  uint16_t value = vane_magnitude;
  unlock();
  return value;
}

bool Wind::isVaneOk() {
  lock();
  bool value = vane_ok;
  unlock();
  return value;
}

float Wind::getSpeedHz() {
  lock();
  float value = speed_hz;
  unlock();
  return value;
}

float Wind::getSpeedMps() {
  lock();
  float value = speed_mps;
  unlock();
  return value;
}

float Wind::getSpeedKn() {
  lock();
  float value = speed_kn;
  unlock();
  return value;
}

float Wind::getSpeedKph() {
  lock();
  float value = speed_kph;
  unlock();
  return value;
}

int Wind::getSpeedBft() {
  lock();
  int value = speed_bft;
  unlock();
  return value;
}

float Wind::getDownwindMps() {
  lock();
  float value = downwind_mps;
  unlock();
  return value;
}

float Wind::getDownwindKn() {
  lock();
  float value = downwind_kn;
  unlock();
  return value;
}

float Wind::getTemperature() {
  lock();
  float value = temperature;
  unlock();
  return value;
}

bool Wind::isTemperatureOk() {
  lock();
  bool value = temperature_ok;
  unlock();
  return value;
}

// Debug dump, gated on the USB host actually being attached for the same
// reason the DEBUG_ macros in wind.ino are: entering the CDC write path with
// nothing draining it can park the calling task.
void Wind::printWind() {
  if (serial == NULL || !(*serial)) {
    return;
  }
  lock();
  serial->printf("Wind dir %.1f (raw %.1f, side %.1f, mag %u, ok %d)  %.2f kn / %.2f m/s / %d bft  temp %.1f C (ok %d)\n",
                 direction, raw_direction, direction_side, vane_magnitude, vane_ok ? 1 : 0,
                 speed_kn, speed_mps, speed_bft, temperature, temperature_ok ? 1 : 0);
  unlock();
}
