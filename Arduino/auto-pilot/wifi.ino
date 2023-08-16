#include <WiFiNINA.h>
#include <TimeLib.h>
#include "arduino_secrets.h"

#define BUF_SIZE 100

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)
//IPAddress ip(10, 10, 10, 100);

IPAddress ip(172, 16, 0, 110);
static WiFiServer debug_server(23);
static WiFiServer command_server(8023);
// WiFiClient client;

char command_buffer[BUF_SIZE];
int command_count = BUF_SIZE;
char debug_buffer[BUF_SIZE];
int debug_count = BUF_SIZE;
int wifi_status = WL_IDLE_STATUS;

boolean command_client_already_connected = false;  // whether or not the client was connected previously
boolean debug_client_already_connected = false;    // whether or not the client was connected previously

void setup_wifi() {
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  WiFi.config(ip);

  // attempt to connect to WiFi network:
  while (wifi_status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(5000);
  }

  // start the servers:
  debug_server.begin();
  command_server.begin();
  // you're connected now, so print out the status:
  print_wifi_status();
}

void process_adjust_bearing(WiFiClient client, char buffer[]) {
  float bearing_adjustment = atof(&buffer[1]);
  autoPilot.adjustHeadingDesired(bearing_adjustment);
  client.println("ok");
}

// TODO check MODE is valid
void process_mode(WiFiClient client, char buffer[]) {
  int new_mode = atoi(&buffer[1]);
  if (new_mode >= 0 && new_mode <= 2) {
    int ret = autoPilot.setMode(new_mode);
    if (ret == 0) {
      client.println("ok");
    } else {
      client.println("Could not set mode, maybe you tried to set mode to navigate before setting a waypoint");
    }
  } else {
    client.println("Invalid mode");
  }
}

void process_print(WiFiClient client) {
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
    client.print("N/A");
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
}

void process_waypoint(WiFiClient client, char buffer[]) {
  char* coordinates = strtok(buffer, ",");
  Serial.print("Settnig waypoint ");
  Serial.print(buffer);
  if (coordinates != NULL) {
    Serial.print("/");
    Serial.print(coordinates);
    Serial.print("/");
    float waypoint_lat = atof(coordinates + 1);
    Serial.print(waypoint_lat,6);
    coordinates = strtok(NULL, ",");
    if (coordinates != NULL) {
      Serial.print("/");
      Serial.print(coordinates);
      Serial.print("/");
      float waypoint_lon = atof(coordinates);
      Serial.println(waypoint_lon);
      autoPilot.setWaypoint(waypoint_lat, waypoint_lon);
      client.println("ok");
    } else {
      client.println("Invalid or missing longitute");
    }
  } else {
    client.println("Invalid or missing latitude");
  }
}

void process_help(WiFiClient client) {
  client.println("Possible commands:");
  client.println("");
  client.println("\ta<heading offset> \t- Adjust heading to be <heading offset> from current heading.");
  client.println("\tw<lat,long> \t\t- Set the waypoint to <lat,long>.");
  client.println("\tm<0|1|2> \t\t- Set the current mode 0 = off, 1 = compass, 2 = navigate.");
  client.println("\t? \t\t\t- Print this help screen");
}

void process_command(WiFiClient client, char buffer[]) {
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

void read_command(WiFiClient& client) {
  // when the client sends the first byte, say hello:
  if (client) {
    if (client.available() > 0) {
      // read the bytes incoming from the client:
      char thisChar = client.read();
      if (thisChar == '\n') {
        command_buffer[BUF_SIZE - command_count] = 0;
        process_command(client, command_buffer);
        command_count = BUF_SIZE;
      } else {
        if (command_count > 0) {
          command_buffer[BUF_SIZE - command_count] = thisChar;
          command_count--;
        }
      }
    }
  }
}

void read_debug(WiFiClient& client) {
  // when the client sends the first byte, say hello:
  if (client) {
    if (client.available() > 0) {
      // read the bytes incoming from the client:
      char thisChar = client.read();
      if (thisChar == '\n') {
        debug_buffer[BUF_SIZE - debug_count] = 0;
        process_command(client, debug_buffer);
        debug_count = BUF_SIZE;
      } else {
        if (debug_count > 0) {
          debug_buffer[BUF_SIZE - debug_count] = thisChar;
          debug_count--;
        }
      }
    }
  }
}

void check_command() {
  // wait for a new client on command server:
  //Serial.println("Checking command");
  WiFiClient command_client = command_server.available();
  if (command_client) {
    if (!command_client_already_connected) {
      // clear out the input buffer:
      Serial.println("We have a new command client");
      command_client.flush();
      command_client_already_connected = true;
    }
    read_command(command_client);
  }
  // wait for a new client on debug server:
  WiFiClient debug_client = debug_server.available();
  if (debug_client) {
    if (!debug_client_already_connected) {
      // clear out the input buffer:
      Serial.println("We have a new debug client");
      debug_client.flush();
      debug_client_already_connected = true;
    }
    read_debug(debug_client);
  }
}


void print_wifi_status() {
  Serial.print("WiFi firmware version: ");
  Serial.println(WiFi.firmwareVersion());

  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}