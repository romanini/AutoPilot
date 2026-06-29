// Interactive telnet server (port 23) for debugging / manual control.
// Machine-to-machine commands from the displays / OpenCPN come in over UDP
// instead - see subscribe.ino.

#include <ESPTelnet.h>
typedef ESPTelnet CustomClientType;
#include <TimeLib.h>

#define BUF_SIZE 100
#define TELNET_PORT 23

static ESPTelnet telnet_server;

char telnet_buffer[BUF_SIZE];
int telnet_count = BUF_SIZE;

// Explicit forward declarations: these signatures use CustomClientType, so they
// must be declared before Arduino's auto-prototype pass (which runs before the
// typedef above is visible at the top of the combined sketch).
void process_adjust_bearing(CustomClientType& client, char buffer[]);
void process_steer_angle(CustomClientType& client, char buffer[]);
void process_mode(CustomClientType& client, char buffer[]);
void process_navigation(CustomClientType& client, char buffer[]);
void process_print(CustomClientType& client);
void process_quit(CustomClientType& client);
void process_waypoint(CustomClientType& client, char buffer[]);
void process_garmin_inject(CustomClientType& client, char buffer[]);
void process_follow_arm(CustomClientType& client, char buffer[]);
void process_help(CustomClientType& client);
void process_telnet(CustomClientType& client, char buffer[]);

int garmin_inject_line(const char* line);  // defined in garmin.ino
void navsource_set_armed(bool armed);      // defined in navsource.ino
bool navsource_is_armed();                 // defined in navsource.ino

void setup_telnet() {
  telnet_server.onConnect(onTelnetConnect);
  telnet_server.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet_server.onReconnect(onTelnetReconnect);
  telnet_server.onDisconnect(onTelnetDisconnect);
  telnet_server.onInputReceived(onTelnetInput);
  telnet_server.begin(TELNET_PORT);
  DEBUG_PRINTLN("Telnet all setup");
}

void process_adjust_bearing(CustomClientType& client, char buffer[]) {
  float bearing_adjustment = atof(&buffer[1]);
  autoPilot.adjustHeadingDesired(bearing_adjustment);
  client.println("ok");
  DEBUG_PRINT("adjust bearing ");
  DEBUG_PRINTLN(bearing_adjustment);
}

void process_steer_angle(CustomClientType& client, char buffer[]) {
  float steer_angle = atof(&buffer[1]);
  autoPilot.setSteerAngle(steer_angle);
  client.println("ok");
  DEBUG_PRINT("set steer angle ");
  DEBUG_PRINTLN(steer_angle);
}

void process_navigation(CustomClientType& client, char buffer[]) {
  int new_nav = atoi(&buffer[1]);
  if (new_nav >= 0 && new_nav <= 1) {
    autoPilot.setNavigationEnabled(new_nav == 1);
    DEBUG_PRINT("set navigation ");
    DEBUG_PRINT(new_nav);
    client.println("ok");
  } else {
    client.println("Invalid Navigation");
  }
}

void process_mode(CustomClientType& client, char buffer[]) {
  int new_mode = atoi(&buffer[1]);
  if (new_mode >= 1 && new_mode <= 2) {
    int ret = autoPilot.setMode(new_mode);
    DEBUG_PRINT("set mode ");
    DEBUG_PRINT(new_mode);
    DEBUG_PRINT(" returned ");
    DEBUG_PRINTLN(ret);
    if (ret == 0) {
      client.println("ok");
    } else {
      client.println("Could not set mode, maybe you tried to set mode to navigate before setting a waypoint");
    }
  } else {
    client.println("Invalid mode");
  }
}

void process_quit(CustomClientType& client) {
  DEBUG_PRINTLN("Closing connection");
  client.println("> disconnecting you");
  client.disconnectClient();
}

void process_print(CustomClientType& client) {
  client.print("Date&Time: ");
  char dateTimeString[16];
  time_t currentTime = autoPilot.getDateTime();
  sprintf(dateTimeString, "%d/%d/%02d %d:%02d", month(currentTime), day(currentTime), year(currentTime) % 100, hour(currentTime), minute(currentTime));
  client.print(dateTimeString);
  if (autoPilot.hasFix()) {
    if (autoPilot.getFixquality() == 0) {
      client.print(" n/a");
    } else if (autoPilot.getFixquality() == 1) {
      client.print(" GPS");
    } else if (autoPilot.getFixquality() == 2) {
      client.print(" DGPS");
    }
    client.print(" (");
    client.print(autoPilot.getSatellites());
    client.print(")");
  }
  client.println("");

  client.print("Navigation: ");
  if (autoPilot.isNavigationEndabled()) {
    client.print("enabled ");
  } else {
    client.print("disabled ");
  }
  client.print("Destination: ");
  if (autoPilot.getMode() == 2) {
    client.print("waypoint ");
    client.print(autoPilot.getWaypointLat(), 6);
    client.print(",");
    client.print(autoPilot.getWaypointLon(), 6);
  } else if (autoPilot.getMode() == 1) {
    client.print("compass ");
    client.print(autoPilot.getHeadingDesired(), 1);
  } else {
    client.print("disabled");
  }
  client.println("");

  client.print("Heading: ");
  client.print(autoPilot.getHeading());
  client.print(" ");
  client.print("Bearing: ");
  if (autoPilot.getMode() > 0) {
    client.print(autoPilot.getBearing(), 1);
    client.print(" ");
    client.print((autoPilot.getBearingCorrection() > 0) ? autoPilot.getBearingCorrection() : autoPilot.getBearingCorrection() * -1.0, 1);
    client.print((autoPilot.getBearingCorrection() > 0) ? " R" : " L");
  } else {
    client.print("N/A");
  }
  client.println("");

  client.print("Speed: ");
  client.print(autoPilot.getSpeed(), 2);
  client.print(" Distance: ");
  client.print(autoPilot.getDistance(), 2);
  client.print(" Course: ");
  client.print(autoPilot.getCourse(), 2);
  client.print(" Location: ");
  client.print(autoPilot.getLocationLat(), 6);
  client.print(",");
  client.print(autoPilot.getLocationLon(), 6);
  client.println("");
  client.println("");
}

void process_waypoint(CustomClientType& client, char buffer[]) {
  // strtok_r (not strtok): the UDP 'w' handler runs in the AsyncUDP task while
  // this runs in command_task. A shared static strtok pointer would corrupt if
  // both parsed a waypoint at once; a local saveptr is reentrant.
  char* saveptr = NULL;
  char* coordinates = strtok_r(buffer, ",", &saveptr);
  if (coordinates != NULL) {
    float waypoint_lat = atof(coordinates + 1);
    coordinates = strtok_r(NULL, ",", &saveptr);
    if (coordinates != NULL) {
      float waypoint_lon = atof(coordinates);
      autoPilot.setWaypoint(waypoint_lat, waypoint_lon);
      DEBUG_PRINT("Waypoint set to: ");
      DEBUG_PRINT2(waypoint_lat, 6);
      DEBUG_PRINT(",");
      DEBUG_PRINTLN2(waypoint_lon, 6);
      client.println("ok");
    } else {
      client.println("Invalid or missing longitute");
    }
  } else {
    client.println("Invalid or missing latitude");
  }
}

// Tier-1 test hook: inject a raw NMEA line into the Garmin receive path exactly
// as check_garmin() would after assembling it from COM1 (plan §2.7 / §1b). Lets
// us exercise the ~APRX relay + checksum filter with no Garmin wired up. The text
// after 'g' must be a complete sentence including the leading '$' and "*CRC".
void process_garmin_inject(CustomClientType& client, char buffer[]) {
  char* line = &buffer[1];  // skip the 'g'
  if (*line == '\0') {
    client.println("usage: g<nmea line, incl. $ and *CRC>");
    return;
  }
  int status = garmin_inject_line(line);
  switch (status) {
    case 0:  client.println("ok - relayed as ~APRX"); break;
    case 1:  client.println("dropped - bad/missing checksum"); break;
    case 2:  client.println("ok - valid but filtered (not WPL/RTE/RMB/XTE/BOD)"); break;
    default: client.println("?"); break;
  }
}

// Arm/disarm Follow-Garmin (plan §2.4). Armed: an RMB status 'A' auto-engages
// waypoint-navigate toward the Garmin destination. Disarmed (default): the RMB
// only populates the waypoint and the operator presses Enable.
void process_follow_arm(CustomClientType& client, char buffer[]) {
  int v = atoi(&buffer[1]);
  navsource_set_armed(v != 0);
  client.println(navsource_is_armed() ? "Follow-Garmin ARMED" : "Follow-Garmin disarmed");
}

void process_help(CustomClientType& client) {
  client.println("Possible commands:\n");
  client.println("\ta<heading offset> \t- Adjust heading to be <heading offset> from current heading.");
  client.println("\tf<0|1> \t\t\t- Arm/disarm Follow-Garmin (auto-engage nav on RMB).");
  client.println("\tg<nmea> \t\t- Inject a Garmin NMEA line (test the ~APRX relay).");
  client.println("\tm<1|2> \t\t\t- Set the mode 1 = compass, 2 = waypoint.");
  client.println("\tn<0|1> \t\t\t- Navigation 0 = off, 1 = on");
  client.println("\tp \t\t\t- Print current auto pilot status.");
  client.println("\tq \t\t\t- Quit the current session.");
  client.println("\tw<lat,long> \t\t- Set the waypoint to <lat,long>.");
  client.println("\t? \t\t\t- Print this help screen.");
}

void process_telnet(CustomClientType& client, char buffer[]) {
  char command = buffer[0];
  bool show_state = false;  // echo current state after state-changing commands
  switch (command) {
    case 'a':
      process_adjust_bearing(client, buffer);
      show_state = true;
      break;
    case 'm':
      process_mode(client, buffer);
      show_state = true;
      break;
    case 'n':
      process_navigation(client, buffer);
      show_state = true;
      break;
    case 'p':
      process_print(client);
      break;
    case 'q':
      process_quit(client);
      break;
    case 's':
      process_steer_angle(client, buffer);
      show_state = true;
      break;
    case 'w':
      process_waypoint(client, buffer);
      show_state = true;
      break;
    case 'g':
      process_garmin_inject(client, buffer);
      break;
    case 'f':
      process_follow_arm(client, buffer);
      break;
    case '?':
      process_help(client);
      break;
    default:
      client.println("-1 Command not understood");
      break;
  }
  // After a command that changes state, echo the full status (same output as the
  // 'p' command) so the operator sees the result. Skipped for 'p' (already
  // printed), '?' (help), 'q' (client disconnected), and unknown commands.
  if (show_state) {
    process_print(client);
  }
}


// (optional) callback functions for telnet events. These run in command_task
// (via telnet.loop()). The DEBUG_* macros self-guard with if(Serial), so a
// telnet connect/disconnect while USB is unplugged can't stall the task on the
// CDC detach deadlock. telnet_server.println() writes go to the TCP client (not
// USB), so they're fine.
void onTelnetConnect(String ip) {
  DEBUG_PRINT("- Telnet: ");
  DEBUG_PRINT(ip);
  DEBUG_PRINTLN(" connected");

  telnet_server.println("\nWelcome " + telnet_server.getIP());
  telnet_server.println("(Use ^] + q  to disconnect.)");
}

void onTelnetDisconnect(String ip) {
  DEBUG_PRINT("- Telnet: ");
  DEBUG_PRINT(ip);
  DEBUG_PRINTLN(" disconnected");
}

void onTelnetReconnect(String ip) {
  DEBUG_PRINT("- Telnet: ");
  DEBUG_PRINT(ip);
  DEBUG_PRINTLN(" reconnected");
}

void onTelnetConnectionAttempt(String ip) {
  DEBUG_PRINT("- Telnet: ");
  DEBUG_PRINT(ip);
  DEBUG_PRINTLN(" tried to connected");
}

void onTelnetInput(String str) {
  int len = str.length() + 1;
  str.toCharArray(telnet_buffer, len);
  telnet_count = BUF_SIZE - len;
  process_telnet(telnet_server, telnet_buffer);
}

void check_telnet() {
  telnet_server.loop();
}
