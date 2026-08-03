#ifndef WIND_H
#define WIND_H

#include <USB.h>
#include <FreeRTOS.h>
typedef USBCDC SerialType;  // Define SerialType for ESP32

// ---------------------------------------------------------------------------
// Physical constants of the Yachta cup-anemometer / vane head.
//
// These live in the header rather than in Wind.cpp because both layers need
// them: Wind.cpp turns a rotation period into a speed, and anemometer.ino has
// to apply the same period limit inside its interrupt handler. One definition,
// so the two can't drift apart.
//
// Values taken verbatim from the original firmware's Definitions.h
// (radius2 / lamda) for the "Yachta" sensor type.
// ---------------------------------------------------------------------------

// Distance from the spindle to the middle of a cup, in metres. Yachta uses
// 0.043; the original also carries 0.06 (WiFi 1000) and 0.055 (Ventus).
#define ANEMOMETER_RADIUS_M 0.043f

// Ratio of cup speed to wind speed for a 3-cup anemometer. Constant of the
// cup geometry, not of this particular build.
#define ANEMOMETER_LAMBDA 0.3f

// The Yachta wheel trips the reed switch twice per revolution (the WiFi 1000
// and Ventus heads trip once).
#define ANEMOMETER_PULSES_PER_REV 2

// Longest pulse interval that still counts as "turning". Intervals are clamped
// to this in the interrupt handler and calculate() then refuses to use a period
// that has reached the limit, so a nearly-stopped anemometer holds its last
// speed rather than reporting an ever-decreasing one. Anything genuinely
// stopped is caught instead by the zero-wind timeout in anemometer.ino.
#define ANEMOMETER_PERIOD_LIMIT_MS 1000.0f

// Thread-safe state model for the masthead wind sensor, in the same shape as
// AutoPilot.{h,cpp} in the controller and display sketches: private fields, a
// recursive mutex, and locked getters/setters.
//
// It carries all of the wind *math* ported from Norbert Walter's Windsensor
// Yachta firmware (https://github.com/norbert-walter/Windsensor_Yachta,
// src/Calculation.h). The .ino files own the hardware: vane.ino reads the
// AS5600, anemometer.ino times the reed switch, temperature.ino reads the
// DS18B20. They push raw values in through the setters; calculate() turns
// those into the derived values publish.ino puts on the wire.
//
// Splitting it this way is what lets the maths be read (and eventually reused
// by a wind-vane steering mode in the controller) without dragging along the
// ESP8266 timer/EEPROM/web-server scaffolding the original interleaved it with.
class Wind {
private:
  SemaphoreHandle_t mutex;

  // ---- raw inputs, written by the sensor layer ----------------------------
  float raw_direction;        // vane bearing relative to the bow, degrees, offset already applied by vane.ino
  uint16_t vane_magnitude;    // AS5600 field strength - diagnostic only, tells you if the magnet gap is right
  bool vane_ok;               // AS5600 reports a magnet in range
  float rotation_period_ms;   // averaged milliseconds between anemometer pulses
  bool rotation_valid;        // false when the anemometer has stopped (see WIND_ZERO_TIMEOUT_US)
  float temperature;          // DS18B20, degrees C, self-heating already compensated
  bool temperature_ok;        // a DS18B20 answered on the 1-Wire bus

  // ---- derived by calculate() ---------------------------------------------
  float direction;            // apparent wind angle 0..360, rate limited
  float direction_side;       // same angle folded to 0..180 for a per-side (L/R) presentation
  bool direction_seeded;      // has direction ever been set? (first sample bypasses the rate limiter)
  float speed_hz;             // anemometer revolutions per second
  float speed_mps;
  float speed_kn;
  float speed_kph;
  int speed_bft;              // Beaufort force, 0..12
  float downwind_mps;         // component of the wind that is astern (see DOWNWIND_RANGE_DEG)
  float downwind_kn;
  unsigned long last_calculation;  // millis() of the previous calculate(), for the rate limiter

  SerialType* serial;

  void lock();
  void unlock();
  static int roundFloat2Int(float x);
  static float normalizeDegrees(float degrees);
  static float degreesDelta(float from, float to);

public:
  Wind(SerialType* ser);
  ~Wind();

  // Inputs from the sensor layer.
  void setVane(float degrees, uint16_t magnitude, bool ok);
  void setRotationPeriod(float period_ms, bool valid);
  void setTemperature(float celsius, bool ok);

  // Turns the raw inputs above into every derived value below. Call at a
  // steady cadence - the direction rate limiter is expressed per second, so it
  // copes with a changed interval, but it works best when the interval is even.
  void calculate();

  float getDirection();
  float getDirectionSide();
  float getRawDirection();
  uint16_t getVaneMagnitude();
  bool isVaneOk();

  float getSpeedHz();
  float getSpeedMps();
  float getSpeedKn();
  float getSpeedKph();
  int getSpeedBft();
  float getDownwindMps();
  float getDownwindKn();

  float getTemperature();
  bool isTemperatureOk();

  void printWind();
};

#endif
