// UDP intake for the rudder position sensor board (Arduino/rudder/). This is
// a separate protocol from the controller<->display pair (8888/8889) - see
// the autopilot skill for the full design. Receiving a telemetry packet here
// is also the *only* way the controller learns the rudder board's IP (there
// is no static assignment), which relay_rudder_command() below depends on.

#include <AsyncUDP.h>

#define RUDDER_TELEMETRY_PORT 8890  // rudder -> controller: ~APRUD,<angle>,<magnet_ok>$
#define RUDDER_COMMAND_PORT 8891    // controller -> rudder: ~APCMD,<cmd>$ (relayed)
#define RUDDER_BUFFER_SIZE 64       // "~APRUD,-123.45,0$" is well under this
#define RUDDER_RECEIVE_TIMEOUT_MS 1000  // no packet in this long -> treat as no data

static AsyncUDP udpRudderServer;
static AsyncUDP udpRudderRelay;  // used only to relay commands to the rudder board

IPAddress rudderIp;
bool rudderIpKnown = false;

// Written from the AsyncUDP task (process_rudder_telemetry), read from
// isRudderOk() (called from command_task via publish_APDAT) - volatile for
// cross-task visibility, same pattern as display/subscribe.ino's lastReceiveTime.
static volatile unsigned long lastRudderReceiveTime = 0;

// Explicit forward declaration (AsyncUDPPacket isn't visible to Arduino's
// auto-prototype pass at the top of the combined sketch - same issue noted in
// subscribe.ino).
void process_rudder_telemetry(AsyncUDPPacket packet);

void setup_rudder() {
  if (udpRudderServer.listen(RUDDER_TELEMETRY_PORT)) {
    udpRudderServer.onPacket(process_rudder_telemetry);
    DEBUG_PRINT("Listening for rudder telemetry on port ");
    DEBUG_PRINTLN(RUDDER_TELEMETRY_PORT);
  } else {
    DEBUG_PRINTLN("Failed to start rudder telemetry listener");
  }
}

// Parses "~APRUD,<angle>,<magnet_ok>$", updates the shared state, and
// remembers the sender's IP for relay_rudder_command() below. Runs in the
// AsyncUDP task context - setRudderAngle() is mutex-protected like every
// other AutoPilot setter, so this is safe against the control/command tasks.
void process_rudder_telemetry(AsyncUDPPacket packet) {
  size_t len = packet.length();
  if (len == 0 || len >= RUDDER_BUFFER_SIZE) {
    return;
  }
  char buffer[RUDDER_BUFFER_SIZE];
  memcpy(buffer, packet.data(), len);
  buffer[len] = '\0';

  if (strncmp(buffer, "~APRUD,", 7) != 0) {
    return;
  }
  char* body = buffer + 7;
  char* end = strchr(body, '$');
  if (end == NULL) {
    return;
  }
  *end = '\0';

  char* saveptr = NULL;
  char* angleStr = strtok_r(body, ",", &saveptr);
  char* magnetStr = strtok_r(NULL, ",", &saveptr);
  if (angleStr == NULL || magnetStr == NULL) {
    return;
  }

  autoPilot.setRudderAngle(atof(angleStr), atoi(magnetStr) == 1);
  lastRudderReceiveTime = millis();

  rudderIp = packet.remoteIP();
  rudderIpKnown = true;
}

// Combines the sensor's own magnet-detected flag with a receive timeout: true
// only if the rudder board reports its magnet detected AND we've heard from it
// within the last second. This - not the raw magnet flag - is what goes out on
// ~APDAT, so a disconnected/powered-off rudder board reads as "no data" rather
// than silently freezing on its last-known value forever.
bool isRudderOk() {
  if (!rudderIpKnown) {
    return false;  // never heard from the rudder board at all
  }
  if (millis() - lastRudderReceiveTime > RUDDER_RECEIVE_TIMEOUT_MS) {
    return false;  // was hearing from it, but not within the timeout window
  }
  return autoPilot.isRudderMagnetOk();
}

// Forwards a command verbatim to the rudder board's last-known IP, e.g. "z"
// (center now) from dispatch_command()'s 'z' case. No-op if we've never heard
// from the rudder board yet - there is no address to relay to.
void relay_rudder_command(const char* cmd) {
  if (!rudderIpKnown) {
    DEBUG_PRINTLN("No known rudder IP yet - dropping relay");
    return;
  }
  char packet[32];
  snprintf(packet, sizeof(packet), "~APCMD,%s$", cmd);
  udpRudderRelay.writeTo((const uint8_t*)packet, strlen(packet), rudderIp, RUDDER_COMMAND_PORT);
  DEBUG_PRINT("relayed to rudder: ");
  DEBUG_PRINTLN(packet);
}
