#include <AsyncUDP.h>

#define PUBLISH_INTERVAL_MS 20
#define TELEMETRY_PORT 8890  // rudder -> controller: ~APRUD,<angle>,<magnet_ok>$

uint32_t lastPublishTime = 0;

IPAddress controllerIp(10, 20, 1, 1);  // controller soft-AP / gateway address
AsyncUDP telemetryUdp;

void read_rudder_angle(float* degreesOut, bool* magnetOkOut);  // defined in angle.ino

// Sends the calibrated angle to the controller. Also serves as the "here I
// am" signal the controller uses to learn (and remember) this board's IP for
// relaying ~APCMD back - see subscribe.ino and the relay design noted in the
// autopilot skill.
void publish_rudder() {
  if (millis() - lastPublishTime < PUBLISH_INTERVAL_MS) {
    return;
  }
  lastPublishTime = millis();
  float degrees;
  bool magnetOk;
  read_rudder_angle(&degrees, &magnetOk);

  char packet[48];
  snprintf(packet, sizeof(packet), "~APRUD,%.2f,%d$", degrees, magnetOk ? 1 : 0);
  telemetryUdp.writeTo((const uint8_t*)packet, strlen(packet), controllerIp, TELEMETRY_PORT);

  DEBUG_PRINTLN(packet);
}
