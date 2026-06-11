#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

//#define MOCK_SEND false
#define COMMAND_LEN 12
#define COMMAND_PORT 8889         // controller's UDP command port (telemetry stays on 8888)
#define COMMAND_LOCAL_PORT 8890   // local port the display binds for sending commands

IPAddress processor_ip(10, 20, 1, 1);  // controller soft-AP / gateway address
WiFiUDP commandUdp;                     // used only to send command datagrams

char command[COMMAND_LEN];

// Commands are sent to the controller as a UDP unicast datagram, framed like the
// other UDP messages on the wire: "~APCMD,<command>$".  Using UDP instead of the
// old telnet/TCP connection means there is no single-client connection limit and
// no connect-time stream flush (ESPTelnet's emptyClientStream() used to discard
// the command the moment it was sent), so multiple displays plus the OpenCPN soft
// unit can all send freely and the controller reliably receives every command.
// Because it is unicast to the controller's IP, no other display ever sees these
// packets, so no filtering is required anywhere.
void send_command(const char* command) {
  char packet[COMMAND_LEN + 16];  // "~APCMD," (7) + command + "$" + NUL
  snprintf(packet, sizeof(packet), "~APCMD,%s$", command);
  commandUdp.beginPacket(processor_ip, COMMAND_PORT);
  commandUdp.write((const uint8_t*)packet, strlen(packet));
  commandUdp.endPacket();
  DEBUG_PRINT("sent command '");
  DEBUG_PRINT(packet);
  DEBUG_PRINTLN("'");
}

void setup_command() {
  // Commands are sent as fire-and-forget UDP datagrams. Bind a local port so the
  // send socket is created up front (rather than relying on lazy creation in
  // beginPacket/endPacket). The controller confirms the resulting state via its
  // 1 Hz telemetry broadcast, which all displays already consume.
  commandUdp.begin(COMMAND_LOCAL_PORT);
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
