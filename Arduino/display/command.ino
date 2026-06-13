#include <WiFi.h>
#include <AsyncUDP.h>

//#define MOCK_SEND false
#define COMMAND_LEN 12
#define COMMAND_PORT 8889  // controller's UDP command port (telemetry stays on 8888)

IPAddress processor_ip(10, 20, 1, 1);  // controller soft-AP / gateway address
AsyncUDP commandUdp;                    // used only to send command datagrams

char command[COMMAND_LEN];

// Commands are sent to the controller as a UDP unicast datagram, framed like the
// other UDP messages on the wire: "~APCMD,<command>$".  UDP has no single-client
// connection limit and no connect-time stream flush, so multiple displays plus
// the OpenCPN soft unit can all send freely and the controller reliably receives
// every command.  Because it is unicast to the controller's IP, no other display
// sees these packets, so no filtering is needed.  AsyncUDP::writeTo creates its
// socket on first use, so there is nothing to set up beforehand.
void send_command(const char* command) {
  char packet[COMMAND_LEN + 16];  // "~APCMD," (7) + command + "$" + NUL
  snprintf(packet, sizeof(packet), "~APCMD,%s$", command);
  commandUdp.writeTo((const uint8_t*)packet, strlen(packet), processor_ip, COMMAND_PORT);
  DEBUG_PRINT("sent command '");
  DEBUG_PRINT(packet);
  DEBUG_PRINTLN("'");
}

void check_command() {
  if (autoPilot.getReset()) {
    autoPilot.setReset(false);
  }
}

void adjust_heading(float change) {
#ifndef MOCK_SEND
  sprintf(command, "a%.2f", change);
  send_command(command);
  autoPilot.adjustHeadingDesired(change);
  DEBUG_PRINT("adjusting heading ");
  DEBUG_PRINTLN(change);
#endif
}

void set_mode(int mode) {
#ifndef MOCK_SEND
  sprintf(command, "m%d", mode);
  send_command(command);
  autoPilot.setMode(mode);
  DEBUG_PRINT("set mode ");
  DEBUG_PRINTLN(mode);
#endif
}

void set_navigation(int nav) {
#ifndef MOCK_SEND
  sprintf(command, "n%d", nav);
  send_command(command);
  autoPilot.setNavigationEnabled(nav == 1);
  DEBUG_PRINT("set navigation ");
  DEBUG_PRINTLN(nav);
#endif
}
