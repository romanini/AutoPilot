#include <AsyncUDP.h>

#define COMMAND_PORT 8893   // controller -> wind: ~APCMD,v$ / ~APCMD,k<slope>,<offset>$
#define CMD_BUFFER_SIZE 48  // longest is "~APCMD,k1.05432,-0.12345$" at 25 - room to spare

static AsyncUDP commandUdp;

// Explicit forward declarations (AsyncUDPPacket isn't visible to Arduino's
// auto-prototype pass at the top of the combined sketch - same issue noted in
// controller/subscribe.ino).
void process_command(AsyncUDPPacket packet);
void request_calibration();                                     // defined in vane.ino
bool request_speed_calibration(float slope, float offset);      // defined in anemometer.ino

// close() first so this is safe to call again after a WiFi reconnect (see
// check_wifi() in wifi.ino) - it rebinds the listening socket cleanly on the
// fresh link instead of failing because the stale one is still open.
void setup_subscribe() {
  commandUdp.close();
  if (commandUdp.listen(COMMAND_PORT)) {
    commandUdp.onPacket(process_command);
    DEBUG_PRINT("Listening for commands on port ");
    DEBUG_PRINTLN(COMMAND_PORT);
  } else {
    DEBUG_PRINTLN("Failed to start command listener");
  }
}

// Two commands, both calibration:
//
//   v                    the vane is pointing dead ahead right now, call this
//                        zero (vane.ino)
//   k<slope>,<offset>    install a wind speed calibration, speed[m/s] =
//                        raw * slope + offset (anemometer.ino)
//
// Anything else is ignored. Single-letter verbs because the controller's
// dispatch_command() (controller/subscribe.ino) switches on the first
// character alone, so a multi-letter verb would collide with whatever single
// letter it starts with; both are free, since a, m, n, w, X, t and z are taken.
// Neither is relayed by the controller yet - for now send them straight to this
// board's port from the navigator.
//
// Runs on the AsyncUDP task, so both handlers only *flag* the work rather than
// doing it: a vane re-zero is an I2C read racing sensor_task's own reads, and
// both write NVS. Doing either here would put flash writes on the network
// stack's task. command_task picks the flags up - see
// check_calibration_request() (vane.ino) and check_speed_calibration_request()
// (anemometer.ino).
void process_command(AsyncUDPPacket packet) {
  size_t len = packet.length();
  if (len == 0 || len >= CMD_BUFFER_SIZE) {
    return;
  }
  char buffer[CMD_BUFFER_SIZE];
  memcpy(buffer, packet.data(), len);
  buffer[len] = '\0';

  if (strncmp(buffer, "~APCMD,", 7) != 0) {
    return;
  }
  char* cmd = buffer + 7;
  char* end = strchr(cmd, '$');
  if (end == NULL) {
    return;
  }
  *end = '\0';

  switch (cmd[0]) {
    case 'v':
      request_calibration();
      break;
    case 'k': {
      // strtok_r (not strtok): this runs in the AsyncUDP task, and a shared
      // static strtok pointer would be corruptible by any other parse running
      // concurrently. Same reasoning as the controller's 'w' handler.
      // request_speed_calibration() validates before anything reaches flash.
      char* saveptr = NULL;
      char* slopeStr = strtok_r(&cmd[1], ",", &saveptr);
      char* offsetStr = strtok_r(NULL, ",", &saveptr);
      if (slopeStr != NULL && offsetStr != NULL) {
        request_speed_calibration(atof(slopeStr), atof(offsetStr));
      }
      break;
    }
    default:
      break;
  }
}
