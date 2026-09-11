#include <AsyncUDP.h>

#define COMMAND_PORT 8891   // controller -> rudder: ~APCMD,z$ (relayed "center now")
#define CMD_BUFFER_SIZE 32  // only ever holds "~APCMD,z$"

static AsyncUDP commandUdp;

// Explicit forward declarations (AsyncUDPPacket isn't visible to Arduino's
// auto-prototype pass at the top of the combined sketch - same issue noted in
// controller/subscribe.ino).
void process_command(AsyncUDPPacket packet);
void request_calibration();  // defined in angle.ino

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

// Handles "~APCMD,z$" relayed by the controller. Any other verb is ignored -
// this listener only ever needs to understand the one command meant for it.
//
// Runs on the AsyncUDP task, so it only *flags* the re-zero rather than doing
// it: calibrating here would perform an I2C read (racing sensor_task's own
// reads) and an NVS flash write on the network stack's task. command_task
// picks the flag up - see check_calibration_request() in angle.ino.
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

  if (cmd[0] == 'z') {
    request_calibration();
  }
}
