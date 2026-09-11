// Interactive telnet server (port 23) for debugging / manual control.
// Machine-to-machine commands from the displays / OpenCPN come in over UDP
// instead - see subscribe.ino.
//
// NOTE: this is a *separate* command surface from dispatch_command() in
// subscribe.ino, with its own switch and its own verb set. Adding a verb there
// does not add it here, and the two do not share a parser. That is deliberate -
// telnet is a human interface and can answer back, where UDP ~APCMD is
// fire-and-forget with no ack by design - but it does mean a command meant to
// be reachable from both has to be wired up twice. The sensor-board calibration
// commands below are the case where that matters most: they are exactly the
// sort of thing you do from a laptop, typing a number and wanting to see what
// landed, which the UDP path structurally cannot tell you.

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
void process_print(CustomClientType& client);
void process_quit(CustomClientType& client);
void process_waypoint(CustomClientType& client, char buffer[]);
void process_garmin_inject(CustomClientType& client, char buffer[]);
void process_rudder_center(CustomClientType& client);
void process_wind_calibration(CustomClientType& client, char buffer[]);
void process_help(CustomClientType& client);
void process_telnet(CustomClientType& client, char buffer[]);
// Static helpers need declaring here for the same reason as the rest: the
// auto-prototype pass would otherwise emit one ahead of the typedef above.
// `static` does not exempt them.
static bool report_relay_reachability(CustomClientType& client, const char* board, long lastHeardMs);

int garmin_inject_line(const char* line);       // defined in garmin.ino
const char* navsource_selected_name();          // defined in navsource.ino
bool relay_rudder_command(const char* cmd);     // defined in rudder.ino
bool relay_wind_command(const char* cmd);       // defined in wind.ino
bool isRudderOk();                              // defined in rudder.ino
bool isWindOk();                                // defined in wind.ino
bool isTrueWindOk();                            // defined in wind.ino
long rudder_last_heard_ms();                    // defined in rudder.ino
long wind_last_heard_ms();                      // defined in wind.ino

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

  client.print("Nav source: ");
  client.println(navsource_selected_name());

  // Motor-enable / kill switch (motorenable.ino). Worth its own line because it
  // now gates navigation: if Navigation below reads "disabled" and nothing on a
  // display will re-enable it, this is the first place to look. The millivolts
  // are the raw divider reading - ~2500 closed, ~0 open.
  client.print("Motor enable: ");
  client.print(motor_enable_switch_on() ? "on" : "off");
  client.print(" (");
  client.print(motor_enable_sense_mv());
  client.println(" mV)");

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

  // Rudder sensor board (firmware/Arduino/rudder/). isRudderOk() rather than the raw
  // magnet flag, so a board that is powered off or off the network reads as no
  // data instead of a frozen last value. This line is also what confirms a 'z'
  // landed - it should read 180.0 right after one.
  client.print("Rudder: ");
  if (isRudderOk()) {
    client.print(autoPilot.getRudderAngle(), 1);
    client.println(" (180 = centered)");
  } else {
    client.println("no data");
  }

  // Masthead wind sensor board (firmware/Arduino/wind/). Same isWindOk() reasoning.
  // Temperature has its own flag because a dead DS18B20 costs nothing that
  // matters, so it is reported separately rather than taking the whole line
  // down with it. The raw rev/s is here because it is the uncorrected quantity
  // a speed calibration is fitted against - see the 'k' command.
  client.print("Wind: ");
  if (isWindOk()) {
    client.print(autoPilot.getWindDirection(), 1);
    client.print(" deg  ");
    client.print(autoPilot.getWindSpeedKn(), 2);
    client.print(" kn / ");
    client.print(autoPilot.getWindSpeedMps(), 2);
    client.print(" m/s / ");
    client.print(autoPilot.getWindSpeedBft());
    client.print(" bft  (raw ");
    client.print(autoPilot.getWindSpeedHz(), 3);
    client.print(" rev/s)  Temp: ");
    if (autoPilot.isWindTempOk()) {
      client.print(autoPilot.getWindTemperature(), 1);
      client.println(" C");
    } else {
      client.println("no data");
    }
  } else {
    client.println("no data");
  }

  // Derived here, not received from the masthead: the apparent wind above with
  // the boat's own motion subtracted (AutoPilot::getTrueWind()). On its own
  // line because it has its own failure mode - no GPS fix means no boat speed
  // to subtract, which costs the true wind while leaving the apparent wind
  // above perfectly good. SOG is echoed because it is the input that makes the
  // two lines differ, and the usual reason they don't differ enough.
  client.print("True wind: ");
  if (isTrueWindOk()) {
    float trueWindAngle = 0.0;
    float trueWindSpeed = 0.0;
    autoPilot.getTrueWind(trueWindAngle, trueWindSpeed);
    client.print(trueWindAngle, 1);
    client.print(" deg  ");
    client.print(trueWindSpeed, 2);
    client.print(" kn  (from ");
    client.print(autoPilot.getSpeed(), 2);
    client.println(" kn SOG)");
  } else if (isWindOk()) {
    client.println("no GPS fix - no boat speed to subtract");
  } else {
    client.println("no data");
  }
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

// A sensor board heard from longer ago than this is reported as quiet. Both
// boards publish continuously - the rudder at 50 Hz, the wind at 5 Hz - so
// anything beyond a couple of seconds means it is powered off, off the network,
// or wedged, not merely between packets.
#define SENSOR_QUIET_MS 2000

// Shared reporting for the relayed sensor-board commands.
//
// What telnet can and cannot tell you about a relay is worth being exact about.
// It CANNOT tell you the board received the datagram, accepted the value, or
// wrote it to flash: no ~APCMD in this project carries an ack, so nothing on
// this path can report any of that. But two failures are entirely knowable
// locally, without any reply, and both are worth catching before the operator
// starts wondering why nothing happened:
//
//   1. We have never heard from that board, so there is no address to send to
//      and the command is definitively going nowhere.
//   2. We have an address, but the board has gone quiet - it was there and now
//      is not. The datagram still goes out (the address may well still be
//      right, and UDP costs nothing), but saying "ok" would be a lie.
//
// Returns true if the command should actually be relayed.
static bool report_relay_reachability(CustomClientType& client, const char* board, long lastHeardMs) {
  if (lastHeardMs < 0) {
    client.print("no ");
    client.print(board);
    client.println(" board has ever been heard from - nothing to relay to");
    return false;
  }
  if (lastHeardMs > SENSOR_QUIET_MS) {
    client.print("warning: last heard from the ");
    client.print(board);
    client.print(" board ");
    client.print(lastHeardMs / 1000);
    client.println("s ago - sending anyway, but it may be off or off the network");
  }
  return true;
}

// "Center now" for the rudder sensor board. The controller has nothing to do
// itself - it just relays, because it is the only address the plugin, the
// displays and this console all already know.
//
// "ok" means sent, not applied - see report_relay_reachability() above. The
// show_state echo after this command prints the rudder angle, which is the
// actual confirmation: it should read 180.0 immediately afterwards.
void process_rudder_center(CustomClientType& client) {
  long age = rudder_last_heard_ms();
  if (!report_relay_reachability(client, "rudder", age)) {
    return;
  }
  relay_rudder_command("z");
  if (age <= SENSOR_QUIET_MS) {
    client.println("ok - sent center to the rudder board (watch the angle below)");
  }
}

// Is this a complete number and nothing else? Syntax only.
//
// The controller deliberately does not know the wind board's accepted *ranges*
// (see wind.ino - the board owns those, and a second copy here would be one
// more thing to keep in step). But "that is not a number at all" is a different
// question, and one telnet can answer without duplicating any policy. Worth
// answering, because atof() turns "kfoo,bar" into a perfectly well-formed
// 0.0,0.0 that the board then silently refuses - which from this end looks
// identical to nothing having happened.
static bool telnet_is_number(const char* text) {
  if (text == NULL || *text == '\0') {
    return false;
  }
  char* endptr = NULL;
  strtod(text, &endptr);
  return endptr != text && *endptr == '\0';
}

// The three masthead wind sensor calibrations: v (vane zero), d<+-degrees>
// (vane trim) and k<slope>,<offset> (speed calibration). All three are relayed
// verbatim; the controller neither parses nor validates the arguments, because
// the wind board owns those bounds and a second copy here would be one more
// thing to keep in step.
//
// Same caveat as the rudder: "ok" means sent, not applied. For 'd' and 'k' the
// wind board may still refuse the value as out of range, and you would only
// see that on its USB console. The wind block in the status echo is the
// practical confirmation - the angle should shift by what you asked for, and
// for a speed calibration the knots should move while the raw rev/s does not.
void process_wind_calibration(CustomClientType& client, char buffer[]) {
  // Validate on a copy: tokenising the real buffer would eat the comma, and
  // what gets relayed has to be the operator's string exactly as typed.
  char scratch[BUF_SIZE];
  strncpy(scratch, buffer, sizeof(scratch) - 1);
  scratch[sizeof(scratch) - 1] = '\0';

  if (buffer[0] == 'd') {
    if (!telnet_is_number(&scratch[1])) {
      client.println("usage: d<+-degrees>  e.g. d-5  (shift reported wind angle)");
      return;
    }
  } else if (buffer[0] == 'k') {
    char* saveptr = NULL;
    char* slope = strtok_r(&scratch[1], ",", &saveptr);
    char* offset = strtok_r(NULL, ",", &saveptr);
    if (!telnet_is_number(slope) || !telnet_is_number(offset)) {
      client.println("usage: k<slope>,<offset>  e.g. k1.05,-0.12  (wind speed fit, m/s)");
      return;
    }
  }

  long age = wind_last_heard_ms();
  if (!report_relay_reachability(client, "wind", age)) {
    return;
  }
  relay_wind_command(buffer);
  if (age <= SENSOR_QUIET_MS) {
    client.print("ok - sent '");
    client.print(buffer);
    client.println("' to the wind board (watch the wind line below)");
  }
}

void process_help(CustomClientType& client) {
  client.println("Possible commands:\n");
  client.println("\ta<heading offset> \t- Adjust heading to be <heading offset> from current heading.");
  client.println("\tg<nmea> \t\t- Inject a Garmin NMEA line (test the ~APRX relay).");
  client.println("\tm<1|2> \t\t\t- Set the mode 1 = compass, 2 = waypoint.");
  client.println("\t\t\t\t  (navigation on/off is the motor-enable switch only)");
  client.println("\tp \t\t\t- Print current auto pilot status.");
  client.println("\tq \t\t\t- Quit the current session.");
  client.println("\tw<lat,long> \t\t- Set the waypoint to <lat,long>.");
  client.println("\tpat \t\t\t- Arm relay auto-tune (only while navigation is disabled).");
  client.println("");
  client.println("Sensor board calibration (relayed - 'ok' means sent, not applied):");
  client.println("\tz \t\t\t- Rudder: the rudder is centered now (should then read 180).");
  client.println("\tv \t\t\t- Wind: the vane points dead ahead now (should then read 0).");
  client.println("\td<+-degrees> \t\t- Wind: shift the reported angle, e.g. d-5.");
  client.println("\t\t\t\t  Find the amount by tacking: sail close-hauled on each");
  client.println("\t\t\t\t  tack with identical trim, and the error is HALF the");
  client.println("\t\t\t\t  difference in angle off the bow. Send its negation.");
  client.println("\tk<slope>,<offset> \t- Wind: speed fit in m/s, e.g. k1.05,-0.12.");
  client.println("\t\t\t\t  Fit off-board against a reference; the raw rev/s in");
  client.println("\t\t\t\t  the status line is the uncorrected value to log.");
  client.println("");
  client.println("\t? \t\t\t- Print this help screen.");
}

void process_telnet(CustomClientType& client, char buffer[]) {
  // "pat" (Pilot Auto-Tune) is the one multi-char command, so it's checked
  // ahead of the single-char switch below (buffer[0] == 'p' is otherwise the
  // 'p' print-status command).
  if (strcmp(buffer, "pat") == 0) {
    if (autotune_try_arm()) {
      client.println("Autotune: armed - ready. Start it within 30s (e.g. from a display) or it will cancel.");
    } else {
      client.println("Autotune: could not arm - disable navigation first, or a tune is already ready/running.");
    }
    process_print(client);
    return;
  }

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
    // 'n' is kept only to answer back. Navigation is now engaged solely by the
    // motor-enable switch (motorenable.ino), and telnet is the one command
    // surface that can say so - the ~APCMD side just ignores 'n' because there
    // is no one there to tell. Removing the case outright would make a typed
    // 'n1' fall through to "Unknown command", which sends the operator looking
    // for a typo instead of at the switch.
    case 'n':
      client.println("Navigation is set by the motor-enable switch on the controller only.");
      show_state = true;  // the Motor enable / Navigation lines below show where it stands
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
    case 'z':
      process_rudder_center(client);
      show_state = true;  // the echoed rudder angle is the only confirmation
      break;
    case 'v':
    case 'd':
    case 'k':
      process_wind_calibration(client, buffer);
      show_state = true;  // likewise, the echoed wind line is the confirmation
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
  // Clamp to the buffer: toCharArray with an oversize len would overflow
  // telnet_buffer and smash adjacent globals. No legitimate command comes
  // close (a full NMEA line for 'g' is <= 82 chars + the command byte).
  if (len > BUF_SIZE) {
    telnet_server.println("-1 Line too long");
    return;
  }
  str.toCharArray(telnet_buffer, len);
  telnet_count = BUF_SIZE - len;
  process_telnet(telnet_server, telnet_buffer);
}

void check_telnet() {
  telnet_server.loop();
}
