#ifndef AUTOPILOT_H
#define AUTOPILOT_H

#include <USB.h>
typedef USBCDC SerialType;  // USB CDC serial on the Arduino Nano ESP32

/*
 * Local mirror of the controller's state, for the cockpit wind display.
 *
 * This is the THIRD AutoPilot.{h,cpp} in the project, after controller/ and
 * display/. Read the autopilot skill's "The AutoPilot class" section before
 * trying to merge any of them: they share a shape, not a role. The controller's
 * copy is the authority and computes navigation in its setters; the display's is
 * an optimistic mirror with buttons behind it; this one is a *read-only* mirror
 * with no buttons at all.
 *
 * That last difference is why this copy is so much smaller, and why the
 * omissions are deliberate rather than unfinished:
 *
 *  - No localCommandTime / suppression window. That machinery exists to stop an
 *    incoming broadcast from clobbering a field the operator just changed
 *    locally. This unit changes nothing locally - it has no buttons and it never
 *    transmits - so there is no local value to protect and suppression would
 *    only add lag.
 *  - No setters other than the parser and the connection flag. Every value here
 *    comes off the wire.
 *  - Only the fields this unit draws are stored. ~APDAT carries 39; this keeps
 *    10. parseAPDAT() still walks all 39 by position (it must - the format is
 *    positional), it just discards what it does not render. Adding a field to
 *    the display later means storing it here, not changing how the walk works.
 */

#define APDAT "APDAT,"
#define RESET "RESET,"

class AutoPilot {
private:
  SemaphoreHandle_t mutex;

  // --- from ~APDAT, in wire order -----------------------------------------
  bool  fix;               // GPS fix; without it SOG is not meaningful
  float heading;           // magnetic heading of the bow, degrees
  float speed;             // SOG, knots (the controller deadbands this below 0.8 kn)

  float wind_angle;        // apparent wind angle, 0-360 clockwise from the bow
  float wind_speed;        // apparent wind speed, knots
  bool  wind_ok;           // controller's isWindOk(): vane magnet detected AND heard from within its 1s timeout

  // True wind is computed on the controller (its AutoPilot::getTrueWind()), not
  // here, so this unit, the other head unit and the OpenCPN plugin can never
  // disagree about it. It is SOG-derived, not speed through water.
  float true_wind_angle;   // 0-360 clockwise from the bow
  float true_wind_speed;   // knots
  bool  true_wind_ok;      // controller's isTrueWindOk(): wind_ok AND a GPS fix

  float air_temperature;   // masthead air temperature, degrees C
  bool  air_temperature_ok;  // a DS18B20 answered - separate flag, since losing it costs nothing else

  // --- local ---------------------------------------------------------------
  // Set by the parser, cleared by the receive timeout in subscribe.ino. This is
  // "are we hearing the controller", not "is the wind sensor healthy" - the two
  // fail independently and the screen says which.
  bool  connected;

  SerialType *serial;

  bool isEmpty(char *p);
  void parseAPDAT(char *sentence);
  void lock();
  void unlock();

public:
  AutoPilot(SerialType *ser);
  ~AutoPilot();
  void init();

  void parse(char *sentence);

  bool  hasFix();
  float getHeading();
  float getSpeed();

  float getWindAngle();
  float getWindSpeed();
  bool  isWindOk();

  float getTrueWindAngle();
  float getTrueWindSpeed();
  bool  isTrueWindOk();

  float getAirTemperature();
  bool  isAirTemperatureOk();

  bool  isConnected();
  void  setConnected(bool connected);
};

#endif
