#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
typedef WiFiClient CustomClientType;
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <ESPTelnet.h>
typedef ESPTelnet CustomClientType;
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif
#include <TimeLib.h>

#define BUF_SIZE 100
#define TELNET_PORT 23
#define COMMAND_PORT 8023

#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
static WiFiServer telnet_server(TELNET_PORT);
static WiFiServer command_server(COMMAND_PORT);
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
static ESPTelnet telnet_server;
static ESPTelnet command_server;
#endif

char command_buffer[BUF_SIZE];
int command_count = BUF_SIZE;
char telnet_buffer[BUF_SIZE];
int telnet_count = BUF_SIZE;

void process_adjust_bearing(CustomClientType& client, char buffer[]);
void process_run(CustomClientType& client, char buffer[]);
void process_mode(CustomClientType& client, char buffer[]);
void process_print(CustomClientType& client);
void process_quit(CustomClientType& client);
void process_waypoint(CustomClientType& client, char buffer[]);
void process_help(CustomClientType& client);
void process_command(CustomClientType& client, char buffer[]);

void setup_command() {
  // start the servers:
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  telnet_server.begin();
  command_server.begin();
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#if DEBUG_ENABLED
  telnet_server.onConnect(onTelnetConnect);
  telnet_server.onConnectionAttempt(onTelnetConnectionAttempt);
  telnet_server.onReconnect(onTelnetReconnect);
  telnet_server.onDisconnect(onTelnetDisconnect);

  command_server.onConnect(onCommandConnect);
  command_server.onConnectionAttempt(onCommandConnectionAttempt);
  command_server.onReconnect(onCommandReconnect);
  command_server.onDisconnect(onCommandDisconnect);
#endif
  telnet_server.onInputReceived(onTelnetInput);
  telnet_server.begin(TELNET_PORT);
  command_server.onInputReceived(onCommandInput);
  command_server.begin(COMMAND_PORT);
#endif
  Serial.println("Command all setup");
}

void process_adjust_bearing(CustomClientType& client, char buffer[]) {
  float bearing_adjustment = atof(&buffer[1]);
  autoPilot.adjustHeadingDesired(bearing_adjustment);
  client.println("ok");
  DEBUG_PRINT("adjust bearing ");
  DEBUG_PRINTLN(bearing_adjustment);
}

void process_run(CustomClientType& client, char buffer[]) {
  float run_millis = atof(&buffer[1]);
  //autoPilot.setStartMotor(run_millis);
  client.println("ok");
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

void process_print(CustomClientType& client) {
  client.print("Date&Time: ");
  char dateTimeString[13];
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
    client.print("navigate ");
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
  client.print(autoPilot.getHeadingLongAverage());
  client.print(" ~");
  client.print(autoPilot.getHeadingLongAverageChange());
  client.print(" / ");
  client.print(autoPilot.getHeadingShortAverage());
  client.print(" ~");
  client.print(autoPilot.getHeadingLongAverageChange());
  client.print(" / ");
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

  client.print("Start Motor: ");
  client.print(autoPilot.getStartMotor());
  client.print(" Motro Started: ");
  client.println((autoPilot.getMotorStarted() ? " Y" : " N"));
}

void process_quit(CustomClientType& client) {
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  client.println("Use ^] + q  to disconnect.");
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  Serial.println("Closing connection");
  client.println("> disconnecting you");
  client.disconnectClient();
#endif
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
  client.println("\tm<0|1|2> \t\t- Set the current mode 0 = off, 1 = compass, 2 = navigate.");
  client.println("\tp \t\t\t- Print current auto pilot status.");
  client.println("\tq \t\t\t- Quit the current session.");
  client.println("\tr<n> \t\t\t- Run motor for N millis, can use positive or negative time to indicate direction.");
  client.println("\tw<lat,long> \t\t- Set the waypoint to <lat,long>.");
  client.println("\t? \t\t\t- Print this help screen.");
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
    case 'p':
      process_print(client);
      break;
    case 'q':
      process_quit(client);
      break;
    case 'r':
      process_run(client, buffer);
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

#if defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)

#if DEBUG_ENABLED
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

#endif

void onTelnetInput(String str) {
  int len = str.length() + 1;
  str.toCharArray(telnet_buffer, len);
  telnet_count = BUF_SIZE - len;
  process_command(telnet_server, telnet_buffer);
}

void onCommandInput(String str) {
  if (str == "q") {
    Serial.println("Closing command connection");
    command_server.println("> disconnecting you");
    command_server.disconnectClient();
  } else {
    int len = str.length() + 1;
    str.toCharArray(command_buffer, len);
    command_count = BUF_SIZE - len;
    process_command(command_server, command_buffer);
  }
}
#endif

void check_command() {
#if defined(ARDUINO_ARCH_SAMD)                           // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  WiFiClient telnet_client = telnet_server.available();  // listen for incoming clients
  if (telnet_client) {                                   // if you get a client,
    if (telnet_client.available()) {                     // if there's bytes to read from the client,
      char c = telnet_client.read();                     // read a byte, then
      if (c == '\n') {                                   // if the byte is a newline character
        telnet_buffer[BUF_SIZE - telnet_count] = 0;
        process_command(telnet_client, telnet_buffer);
        telnet_count = BUF_SIZE;
      } else {
        if (telnet_count > 0) {                        // if you got anything else but a carriage return character,
          telnet_buffer[BUF_SIZE - telnet_count] = c;  // add it to the end of the currentLine
          telnet_count--;
        }
      }
    }
  }
  WiFiClient command_client = command_server.available();  // listen for incoming clients
  if (command_client) {                                    // if you get a client,
    if (command_client.available()) {                      // if there's bytes to read from the client,
      char c = command_client.read();                      // read a byte, then
      if (c == '\n') {                                     // if the byte is a newline character
        command_buffer[BUF_SIZE - command_count] = 0;
        process_command(command_client, command_buffer);
        command_count = BUF_SIZE;
      } else {
        if (command_count > 0) {                         // if you got anything else but a carriage return character,
          command_buffer[BUF_SIZE - command_count] = c;  // add it to the end of the currentLine
          command_count--;
        }
      }
    }
  }
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  telnet_server.loop();
  command_server.loop();
#endif
}
