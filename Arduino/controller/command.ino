
#include <ESPTelnet.h>
typedef ESPTelnet CustomClientType;
#include <TimeLib.h>

#define BUF_SIZE 100
#define TELNET_PORT 23
#define COMMAND_PORT 8023

static ESPTelnet telnet_server;
static ESPTelnet command_server;

char command_buffer[BUF_SIZE];
int command_count = BUF_SIZE;
char telnet_buffer[BUF_SIZE];
int telnet_count = BUF_SIZE;

void process_adjust_bearing(CustomClientType& client, char buffer[]);
void process_steer_angle(CustomClientType& client, char buffer[]);
void process_mode(CustomClientType& client, char buffer[]);
void process_print(CustomClientType& client);
void process_quit(CustomClientType& client);
void process_waypoint(CustomClientType& client, char buffer[]);
void process_help(CustomClientType& client);
void process_telnet(CustomClientType& client, char buffer[]);
void process_command(CustomClientType& client, char buffer[]);

void setup_command() {
  // start the servers:

  telnet_server.onConnect(onTelnetConnect);
  telnet_server.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet_server.onReconnect(onTelnetReconnect);
  telnet_server.onDisconnect(onTelnetDisconnect);  
  command_server.onConnect(onCommandConnect);
  command_server.onConnectionAttempt(onCommandConnectionAttempt);
  command_server.onReconnect(onCommandReconnect);
  command_server.onDisconnect(onCommandDisconnect);
  telnet_server.onInputReceived(onTelnetInput);
  telnet_server.begin(TELNET_PORT);
  command_server.onInputReceived(onCommandInput);
  command_server.begin(COMMAND_PORT);
  Serial.println("Command all setup");
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
  if (new_mode >= 0 && new_mode <= 2) {
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
  Serial.println("Closing connection");
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
  char* coordinates = strtok(buffer, ",");
  if (coordinates != NULL) {
    float waypoint_lat = atof(coordinates + 1);
    coordinates = strtok(NULL, ",");
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

void process_help(CustomClientType& client) {
  client.println("Possible commands:\n");
  client.println("\ta<heading offset> \t- Adjust heading to be <heading offset> from current heading.");
  client.println("\tm<0|1|2> \t\t- Set the current mode 0 = off, 1 = compass, 2 = waypoint.");
  client.println("\tp \t\t\t- Print current auto pilot status.");
  client.println("\tq \t\t\t- Quit the current session.");
  client.println("\tw<lat,long> \t\t- Set the waypoint to <lat,long>.");
  client.println("\t? \t\t\t- Print this help screen.");
}

void process_telnet(CustomClientType& client, char buffer[]) {
  char command = buffer[0];
  switch (command) {
    case 'a':
      process_adjust_bearing(client, buffer);
      break;
    case 'm':
      process_mode(client, buffer);
      break;
    case 'p':
      process_print(client);
      break;
    case 'q':
      process_quit(client);
      break;
    case 's':
      process_steer_angle(client, buffer);
      break;
    case 'w':
      process_waypoint(client, buffer);
      break;
    case '?':
      process_help(client);
      break;
    default:
      client.println("-1 Command not understood");
      break;
  }
}

void process_command(CustomClientType& client, char buffer[]) {
  char command = buffer[0];
  switch (command) {
    case 'a':
      process_adjust_bearing(client, buffer);
      break;
    case 'm':
      process_mode(client, buffer);
      break;
    case 'q':
      process_quit(client);
      break;
    default:
      client.println("-1 Command not understood");
      break;
  }
}


// (optional) callback functions for telnet events
void onTelnetConnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" connected");

  telnet_server.println("\nWelcome " + telnet_server.getIP());
  telnet_server.println("(Use ^] + q  to disconnect.)");
}

void onTelnetDisconnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" disconnected");
}

void onTelnetReconnect(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" reconnected");
}

void onTelnetConnectionAttempt(String ip) {
  Serial.print("- Telnet: ");
  Serial.print(ip);
  Serial.println(" tried to connected");
}

void onCommandConnect(String ip) {
  Serial.print("- Command: ");
  Serial.print(ip);
  Serial.println(" connected");
}

void onCommandDisconnect(String ip) {
  Serial.print("- Command: ");
  Serial.print(ip);
  Serial.println(" disconnected");
}

void onCommandReconnect(String ip) {
  Serial.print("- Command: ");
  Serial.print(ip);
  Serial.println(" reconnected");
}

void onCommandConnectionAttempt(String ip) {
  Serial.print("- Command: ");
  Serial.print(ip);
  Serial.println(" tried to connected");
}

void onTelnetInput(String str) {
  int len = str.length() + 1;
  str.toCharArray(telnet_buffer, len);
  telnet_count = BUF_SIZE - len;
  process_telnet(telnet_server, telnet_buffer);
}

void onCommandInput(String str) {
  Serial.print("Got command input: '");
  Serial.print(str);
  Serial.println("'");

  int len = str.length() + 1;
  str.toCharArray(command_buffer, len);
  command_count = BUF_SIZE - len;
  process_command(command_server, command_buffer);
  command_server.disconnectClient();
}

void check_command() {
  telnet_server.loop();
  command_server.loop();
}
