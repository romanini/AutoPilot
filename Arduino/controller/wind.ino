// UDP intake for the masthead wind sensor board (Arduino/wind/). This is a
// separate protocol from the controller<->display pair (8888/8889) and from the
// rudder board's pair (8890/8891) - see the autopilot skill for the full
// design. Receiving a telemetry packet here is also the *only* way the
// controller learns the wind board's IP (there is no static assignment), which
// relay_wind_command() below depends on.
//
// Deliberately the same shape as rudder.ino, which it was written from. The one
// structural difference is that the wind board takes commands that carry
// arguments (a vane trim and a speed calibration), where the rudder board's
// only command is a bare verb.

#include <AsyncUDP.h>

#define WIND_TELEMETRY_PORT 8892  // wind -> controller: ~APWND,...$
#define WIND_COMMAND_PORT 8893    // controller -> wind: ~APCMD,<cmd>$ (relayed)
#define WIND_BUFFER_SIZE 128      // a full ~APWND is well under this
#define WIND_RECEIVE_TIMEOUT_MS 1000  // no packet in this long -> treat as no data

static AsyncUDP udpWindServer;
static AsyncUDP udpWindRelay;  // used only to relay commands to the wind board

IPAddress windIp;
bool windIpKnown = false;

// Written from the AsyncUDP task (process_wind_telemetry), read from
// isWindOk() (called from command_task) - volatile for cross-task visibility,
// same pattern as rudder.ino's lastRudderReceiveTime.
static volatile unsigned long lastWindReceiveTime = 0;

// Explicit forward declaration (AsyncUDPPacket isn't visible to Arduino's
// auto-prototype pass at the top of the combined sketch - same issue noted in
// subscribe.ino).
void process_wind_telemetry(AsyncUDPPacket packet);

void setup_wind() {
  if (udpWindServer.listen(WIND_TELEMETRY_PORT)) {
    udpWindServer.onPacket(process_wind_telemetry);
    DEBUG_PRINT("Listening for wind telemetry on port ");
    DEBUG_PRINTLN(WIND_TELEMETRY_PORT);
  } else {
    DEBUG_PRINTLN("Failed to start wind telemetry listener");
  }
}

// Parses "~APWND,<direction>,<speed_kn>,<speed_mps>,<bft>,<temp_c>,<vane_ok>,<temp_ok>,<speed_hz>$",
// updates the shared state, and remembers the sender's IP for
// relay_wind_command() below. Runs in the AsyncUDP task context - setWind() is
// mutex-protected like every other AutoPilot setter, so this is safe against
// the control/command tasks.
//
// speed_hz is treated as optional: it was appended to ~APWND after the first
// seven fields existed, and the project's convention is that trailing fields
// may be absent. A wind board running older firmware still parses, just with a
// raw rate of 0 - which is honest, since it isn't sending one.
void process_wind_telemetry(AsyncUDPPacket packet) {
  size_t len = packet.length();
  if (len == 0 || len >= WIND_BUFFER_SIZE) {
    return;
  }
  char buffer[WIND_BUFFER_SIZE];
  memcpy(buffer, packet.data(), len);
  buffer[len] = '\0';

  if (strncmp(buffer, "~APWND,", 7) != 0) {
    return;
  }
  char* body = buffer + 7;
  char* end = strchr(body, '$');
  if (end == NULL) {
    return;
  }
  *end = '\0';

  // strtok_r (not strtok): this runs in the AsyncUDP task alongside the ~APRUD
  // parser and the 'w' command handler, all of which tokenise. A shared static
  // strtok pointer would corrupt if two ran at once; a local saveptr is
  // reentrant. Same reasoning as rudder.ino and subscribe.ino.
  char* saveptr = NULL;
  char* directionStr = strtok_r(body, ",", &saveptr);
  char* speedKnStr = strtok_r(NULL, ",", &saveptr);
  char* speedMpsStr = strtok_r(NULL, ",", &saveptr);
  char* bftStr = strtok_r(NULL, ",", &saveptr);
  char* tempStr = strtok_r(NULL, ",", &saveptr);
  char* vaneOkStr = strtok_r(NULL, ",", &saveptr);
  char* tempOkStr = strtok_r(NULL, ",", &saveptr);
  char* speedHzStr = strtok_r(NULL, ",", &saveptr);  // optional trailing field

  if (directionStr == NULL || speedKnStr == NULL || speedMpsStr == NULL ||
      bftStr == NULL || tempStr == NULL || vaneOkStr == NULL || tempOkStr == NULL) {
    return;  // short frame - ignore rather than store a partial reading
  }

  autoPilot.setWind(atof(directionStr),
                    atof(speedKnStr),
                    atof(speedMpsStr),
                    atoi(bftStr),
                    atof(tempStr),
                    atoi(vaneOkStr) == 1,
                    atoi(tempOkStr) == 1,
                    speedHzStr != NULL ? atof(speedHzStr) : 0.0);
  lastWindReceiveTime = millis();

  windIp = packet.remoteIP();
  windIpKnown = true;
}

// Combines the wind board's own vane flag with a receive timeout: true only if
// the board reports its vane magnet detected AND we've heard from it within the
// last second. This - not the raw vane flag - is what any consumer should ask,
// so a disconnected/powered-off wind board reads as "no data" rather than
// silently freezing on its last-known value forever. Exactly the reasoning
// behind isRudderOk() in rudder.ino.
//
// Note it deliberately does NOT fold in the temperature flag: a dead DS18B20
// costs nothing that matters, while wind angle and speed keep working. The two
// failures are independent, which is why the board sends them as two flags.
bool isWindOk() {
  if (!windIpKnown) {
    return false;  // never heard from the wind board at all
  }
  if (millis() - lastWindReceiveTime > WIND_RECEIVE_TIMEOUT_MS) {
    return false;  // was hearing from it, but not within the timeout window
  }
  return autoPilot.isWindVaneOk();
}

// Milliseconds since the last ~APWND, or -1 if we have never heard from the
// wind board at all.
//
// Exposed for the telnet console, which uses it to tell "there is nowhere to
// send this" apart from "sent, but that board has gone quiet" - a distinction
// relay_wind_command() cannot make on its own, because the remembered IP never
// expires once set. Those are the two relay failures that are knowable locally,
// and telnet reports both; whether the board *accepted* a value is not knowable
// from here at all.
//
// Deliberately not isWindOk(): that folds in the vane flag, and a board whose
// vane magnet has failed is still perfectly reachable for a speed calibration.
// The question here is only "is this board talking to us".
long wind_last_heard_ms() {
  if (!windIpKnown) {
    return -1;
  }
  return (long)(millis() - lastWindReceiveTime);
}

// Forwards a command verbatim to the wind board's last-known IP - "v" (vane
// zero), "d<degrees>" (vane trim) or "k<slope>,<offset>" (speed calibration),
// from dispatch_command() or the telnet console. Passing the whole verb string
// through untouched is what lets the two commands that carry arguments work
// without the controller needing to understand either of them.
//
// Returns false if we've never heard from the wind board, so callers with
// somewhere to report it (telnet) can say so rather than leaving the operator
// wondering why nothing happened.
bool relay_wind_command(const char* cmd) {
  if (!windIpKnown) {
    DEBUG_PRINTLN("No known wind IP yet - dropping relay");
    return false;
  }
  char packet[64];
  snprintf(packet, sizeof(packet), "~APCMD,%s$", cmd);
  udpWindRelay.writeTo((const uint8_t*)packet, strlen(packet), windIp, WIND_COMMAND_PORT);
  DEBUG_PRINT("relayed to wind: ");
  DEBUG_PRINTLN(packet);
  return true;
}
