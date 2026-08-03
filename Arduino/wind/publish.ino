#include <AsyncUDP.h>

// Publish cadence. The measurement underneath only changes as fast as the cups
// turn - about 11 pulses a second in 10 knots, far fewer in light air - so
// there is nothing to gain from going faster, and most packets at 5 Hz are
// already repeats. The rate is chosen for liveness rather than for resolution:
// it gives the controller a steady heartbeat to hang a receive timeout off
// (the same trick isRudderOk() uses in controller/rudder.ino to tell "no wind"
// apart from "no wind sensor"), at five packets per timeout window.
#define PUBLISH_INTERVAL_MS 200

#define TELEMETRY_PORT 8892  // wind -> controller: ~APWND,...$

static uint32_t lastPublishTime = 0;

static IPAddress controllerIp(10, 20, 1, 1);  // controller soft-AP / gateway address
static AsyncUDP telemetryUdp;

// Sends the current wind state to the controller. Also serves as the "here I
// am" signal the controller uses to learn (and remember) this board's IP for
// relaying ~APCMD back - see subscribe.ino, and the same arrangement for the
// rudder board in controller/rudder.ino.
//
// Wire format:
//
//   ~APWND,<direction>,<speed_kn>,<speed_mps>,<bft>,<temp_c>,<vane_ok>,<temp_ok>$
//
//   direction   apparent wind angle, 0..360 degrees clockwise from the bow
//   speed_kn    apparent wind speed in knots
//   speed_mps   the same speed in metres per second (km/h is speed_mps * 3.6)
//   bft         Beaufort force, 0..12
//   temp_c      masthead air temperature in degrees C
//   vane_ok     1 when the AS5600 can see its magnet
//   temp_ok     1 when a DS18B20 answered on the 1-Wire bus
//
// The two health flags are separate rather than combined because the failures
// are independent and mean different things: a dead vane costs direction while
// speed keeps working, a dead DS18B20 costs nothing that matters. Neither flag
// covers "this board stopped transmitting" - that is the controller's job, by
// timing out on these packets, for exactly the reason spelled out for the
// rudder board in the autopilot skill.
void publish_wind() {
  if (millis() - lastPublishTime < PUBLISH_INTERVAL_MS) {
    return;
  }
  lastPublishTime = millis();

  char packet[80];
  snprintf(packet, sizeof(packet), "~APWND,%.1f,%.2f,%.2f,%d,%.1f,%d,%d$",
           wind.getDirection(),
           wind.getSpeedKn(),
           wind.getSpeedMps(),
           wind.getSpeedBft(),
           wind.getTemperature(),
           wind.isVaneOk() ? 1 : 0,
           wind.isTemperatureOk() ? 1 : 0);
  telemetryUdp.writeTo((const uint8_t*)packet, strlen(packet), controllerIp, TELEMETRY_PORT);

  DEBUG_PRINTLN(packet);
}
