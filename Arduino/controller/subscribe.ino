// UDP command intake (port 8889). The displays and the OpenCPN soft unit send
// commands as connectionless datagrams framed as "~APCMD,<command>$".  This is
// the counterpart to publish.ino (which broadcasts telemetry): the controller
// "subscribes" to incoming commands here.  UDP has no single-client connection
// limit, so any number of senders can issue commands concurrently, and unicast
// to the controller means no other display sees them (no filtering needed).

#include <AsyncUDP.h>

#define UDP_COMMAND_PORT 8889    // displays / OpenCPN send "~APCMD,<cmd>$" datagrams here
#define CMD_BUFFER_SIZE 100      // max command datagram we will accept

static AsyncUDP udpCommandServer;

// Explicit forward declarations (AsyncUDPPacket isn't visible to Arduino's
// auto-prototype pass at the top of the combined sketch).
void process_udp_command(AsyncUDPPacket packet);
void dispatch_command(char buffer[]);
void garmin_write_line(const char* nmea);  // defined in garmin.ino

void setup_subscribe() {
  if (udpCommandServer.listen(UDP_COMMAND_PORT)) {
    udpCommandServer.onPacket(process_udp_command);
    DEBUG_PRINT("UDP command server listening on port ");
    DEBUG_PRINTLN(UDP_COMMAND_PORT);
  } else {
    DEBUG_PRINTLN("Failed to start UDP command server");
  }
  DEBUG_PRINTLN("Subscribe all setup");
}

// Handle a command datagram of the form "~APCMD,<command>$".
// Runs in the AsyncUDP task context; all autopilot access below is mutex
// protected, so this is safe against the control and command tasks.
void process_udp_command(AsyncUDPPacket packet) {
  size_t len = packet.length();
  if (len == 0 || len >= CMD_BUFFER_SIZE) {
    return;  // empty, or too large to be one of our commands
  }
  char buffer[CMD_BUFFER_SIZE];
  memcpy(buffer, packet.data(), len);
  buffer[len] = '\0';

  // ~APTX: a raw NMEA line to forward to the Garmin UART. Handle this before the
  // ~APCMD check (which rejects anything else). The inner payload is itself NMEA
  // and contains a '$', so we can't find the frame end with strchr/strrchr -- the
  // outer terminator is definitionally the LAST byte of the datagram. Require it
  // and strip just that one char, leaving the inner "$...*HH" intact.
  if (strncmp(buffer, "~APTX,", 6) == 0) {
    if (buffer[len - 1] != '$') {
      return;                          // not a terminated frame - ignore
    }
    buffer[len - 1] = '\0';            // trim the outer '$'
    garmin_write_line(buffer + 6);     // skip past "~APTX,"
    return;
  }

  if (strncmp(buffer, "~APCMD,", 7) != 0) {
    return;  // not a command frame - ignore
  }
  char* cmd = buffer + 7;        // skip past "~APCMD,"
  char* end = strchr(cmd, '$');  // find the frame terminator
  if (end == NULL) {
    return;  // unterminated frame - ignore
  }
  *end = '\0';                   // trim the trailing '$'

  DEBUG_PRINT("Got UDP command: '");
  DEBUG_PRINT(cmd);
  DEBUG_PRINTLN("'");
  dispatch_command(cmd);
}

// Apply a command (e.g. "n1", "m2", "a-10.00", "w37.1,-122.3"). There is no
// client to reply to - the sender learns the new state from the next telemetry
// broadcast - so we just validate and update the autopilot directly.
void dispatch_command(char buffer[]) {
  switch (buffer[0]) {
    case 'a': {
      float adjustment = atof(&buffer[1]);
      autoPilot.adjustHeadingDesired(adjustment);
      break;
    }
    case 'm': {
      int new_mode = atoi(&buffer[1]);
      if (new_mode >= 1 && new_mode <= 2) {
        autoPilot.setMode(new_mode);
      }
      break;
    }
    case 'n': {
      int new_nav = atoi(&buffer[1]);
      if (new_nav >= 0 && new_nav <= 1) {
        autoPilot.setNavigationEnabled(new_nav == 1);
      }
      break;
    }
    case 'w': {
      // strtok_r (not strtok): this runs in the AsyncUDP task and the telnet
      // 'w' handler runs in command_task. A shared static strtok pointer would
      // corrupt if both parsed a waypoint at once; a local saveptr is reentrant.
      char* saveptr = NULL;
      char* coordinates = strtok_r(&buffer[1], ",", &saveptr);
      if (coordinates != NULL) {
        float lat = atof(coordinates);
        coordinates = strtok_r(NULL, ",", &saveptr);
        if (coordinates != NULL) {
          autoPilot.setWaypoint(lat, atof(coordinates));
        }
      }
      break;
    }
    default:
      break;
  }
}
