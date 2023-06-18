#include <SPI.h>
#include <WiFiNINA.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_LIS2MDL.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include "arduino_secrets.h"

// These are 'flexible' lines that can be changed
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8 // RST can be set to -1 if you tie it to Arduino's reset
#define DISPLAY_UPDATE_RATE 1000

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = SECRET_SSID;    // your network SSID (name)
char pass[] = SECRET_PASS;    // your network password (use for WPA, or use as key for WEP)
IPAddress ip(10, 10, 10, 100);

int status = WL_IDLE_STATUS;
uint32_t display_refresh_timer = millis();

Adafruit_LIS2MDL mag = Adafruit_LIS2MDL(12345);
// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);

WiFiServer debug_server(23);
WiFiServer command_server(8023);
// WiFiClient client;

#define BUF_SIZE 100
#define MAX_MOTOR_PLUS 255
#define MAX_MOTOR_NEG 255
#define MOTOR_PLUS_PIN 2
#define MOTOR_NEG_PIN 3
#define DIRECTION_POSITIVE "port"
#define DIRECTION_NEGATIVE "starbord"
#define COMPASS_READ_INTERVAL 20 // read at 50Hz for high accuracy
#define COMPASS_SHORT_AVERAGE_SIZE 100
#define COMPASS_LONG_AVERAGE_SIZE 1000

char command_buffer[BUF_SIZE];
int command_count = BUF_SIZE;
char debug_buffer[BUF_SIZE];
int debug_count = BUF_SIZE;


float speed = 0;
float distance = 0;
int hour = 0;
int minute = 0;
int day = 0;
int month = 0;
int year = 0;

int wheel = 0;
bool fix = false;
uint fixquality = 0;
int satellites = 13;
float course = 172.45;

int motor_stop_time_mills=0;
unsigned int last_compass_read_time_mills = 0;

float heading_short_average = 0;
float heading_long_average = 0;
float heading_short_average_change = 0;
float heading_long_average_change = 0;

int enabled = 0;
char mode[BUF_SIZE] = { 0 };
float heading = 0; // actual direction of travel or where the bow is pointing
float course_correction = 0;
float heading_desired = 0; // desired heading if navigating by comapss
float bearing = 0; // desired direction of travel or where we want to move towards
float waypoint_lat = 0;
float waypoint_lon = 0;
float location_lat = 0;
float location_long = 0;

boolean command_client_already_connected = false; // whether or not the client was connected previously
boolean debug_client_already_connected = false; // whether or not the client was connected previously

void setup() {
  pinMode(MOTOR_PLUS_PIN,OUTPUT);
  pinMode(MOTOR_NEG_PIN,OUTPUT);
  analogWrite(MOTOR_PLUS_PIN,0);
  analogWrite(MOTOR_NEG_PIN,0);
  //Initialize serial 
  Serial.begin(38400);

  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true);
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  WiFi.config(ip);

  // attempt to connect to WiFi network:
  while (status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);

    // wait 10 seconds for connection:
    delay(5000);
  }

  mag.setDataRate(LIS2MDL_RATE_50_HZ);

  /* Initialise the sensor */
  if(!mag.begin())
  {
    /* There was a problem detecting the LIS2MDL ... check your connections */
    Serial.println("Ooops, no LIS2MDL detected ... Check your wiring!");
    while(1);
  }

  // start the servers:
  debug_server.begin();
  command_server.begin();
  // you're connected now, so print out the status:
  print_wifi_status();
}

void print_wifi_status() {
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

void process_wheel(WiFiClient client, char buffer[]) {
  float value = atof(&buffer[1]);
  int run_mills = value * 1000;
  start_motor(run_mills);
  client.println("ok");
}

void process_bearing(WiFiClient client, char buffer[]) {
  bearing = atof(&buffer[1]);
  Serial.print("Bearing is ");
  Serial.println(bearing);
  client.println("ok");
}

void process_mode(WiFiClient client, char buffer[]) {
  strcpy(mode,&buffer[1]);
  mode[strlen(buffer)] = '\0';
  Serial.print("Mode is ");
  Serial.println(mode);
  if (strcmp(mode,"compass") == 0) {
    bearing = heading_long_average;
    Serial.print("New Bearing ");
    Serial.println(bearing);    
  }
  client.println("ok");
}

void process_destination(WiFiClient client, char buffer[]) {
  char *coordinates = strtok(buffer, ",");
  float latitude = 0;
  float longitude = 0;
  if (coordinates != NULL) {
      waypoint_lat = atof(coordinates);
      coordinates = strtok(NULL, ",");
      if (coordinates != NULL) {
        waypoint_lon = atof(coordinates);
      }
  }
}

void process_enabled(WiFiClient client, char buffer[]) {
  enabled = atoi(&buffer[1]);
  Serial.print("Enabled is ");
  Serial.println(enabled);
  client.println("ok");
}

void process_info(WiFiClient client) {
  Serial.println("Displaying info");
  client.print("Bearing: ");
  client.println(bearing);
  client.print("Heading: ");
  client.print(heading);
  client.print(" adjust by ");
  client.println(course_correction);
  client.print("Mode: ");
  client.println(mode);
  client.print("Enabled: ");
  client.println(enabled);
}

void process_adjust_bearing(WiFiClient client, char buffer[]) {
  bearing += atof(&buffer[2]);
  Serial.print("New bearing is ");
  Serial.println(bearing);
  client.println("ok");
}

void process_adjust_heading(WiFiClient client, char buffer[]) {
  course_correction += atof(&buffer[2]);
  Serial.print("Heading adjust is ");
  Serial.println(course_correction);
  client.println("ok");
}

void process_help(WiFiClient client) {
  client.println("Possible commands:");
  client.println("");
  client.println("\tab<bering offset> \t- Adjust bearing to be <bearing offset> from current bearing.");
  client.println("\tah<heading offset> \t- Adjust heading to be <heading offset> from current heading."); 
  client.println("\tb<bearing> \t\t- Set the current to <bearing>.");
  client.println("\td<lat,long> \t\t- Set the destination to <lat,long>.");
  client.println("\te<0|1> \t\t\t- Set enabled to be the specified value.  0 = disabled and  1 = enabled.");
  client.println("\ti \t\t\t- Display current information such as enabled, mode, heading etc.");
  client.println("\tl<lat,long> \t\t- Set the current location to <lat,long>.");
  client.println("\tm<gps|comapss> \t\t- Set the current mode to the specified mode.");
  client.println("\tw<time in seconds> \t- Start rotatig the wheel for <time in seconds>.");
  client.println("\t? \t\t\t- Print this help screen");
}

void process_command(WiFiClient client, char buffer[]) {
  char command = buffer[0];
  switch (command) {
    case 'a':
      process_adjust_command(client, buffer);
      break;
    case 'b':
      process_bearing(client, buffer);
      break;
    case 'd':
      process_destination(client, buffer);
      break;
    case 'e':
      process_enabled(client, buffer);
      break;
    case 'i':
      process_info(client);
      break;
    case 'm':
      process_mode(client, buffer);
      break;
    case 'w':
      process_wheel(client, buffer);
      break;
    case '?':
      process_help(client);
      break;
    default:
      client.println("-1 Command not understood");
      break;
  }
}

void process_adjust_command(WiFiClient client, char buffer[]){
  char sub_command = buffer[1];
  switch (sub_command) {
    case 'b':
      process_adjust_bearing(client, buffer);
      break;
    case 'h':
      process_adjust_heading(client, buffer);
      break;
    default:
      Serial.println("-1 Adjust sub-Command not understood");
      break;
  }  
}

void read_command(WiFiClient client) {
  // when the client sends the first byte, say hello:
  if (client) {
    if (client.available() > 0) {
      // read the bytes incoming from the client:
      char thisChar = client.read();
      if (thisChar == '\n') {
        command_buffer[BUF_SIZE - command_count]=0;
        process_command(client, command_buffer);
        command_count = BUF_SIZE;
      } else {
        if (command_count > 0) {
          command_buffer[BUF_SIZE - command_count]=thisChar;
          command_count--;
        }
      }
    }
  }
}

void read_debug(WiFiClient client) {
  // when the client sends the first byte, say hello:
  if (client) {
    if (client.available() > 0) {
      // read the bytes incoming from the client:
      char thisChar = client.read();
      if (thisChar == '\n') {
        debug_buffer[BUF_SIZE - debug_count]=0;
        process_command(client, debug_buffer);
        debug_count = BUF_SIZE;
      } else {
        if (debug_count > 0) {
          debug_buffer[BUF_SIZE - debug_count]=thisChar;
          debug_count--;
        }
      }
    }
  }
}

void start_motor(int run_mills) {
  analogWrite(MOTOR_PLUS_PIN,0);
  analogWrite(MOTOR_NEG_PIN,0);
  Serial.print("Turning wheel to ");
  if (run_mills > 0) {
    analogWrite(MOTOR_PLUS_PIN,MAX_MOTOR_PLUS);
    Serial.print(DIRECTION_POSITIVE);
  } else if (run_mills < 0) {
    analogWrite(MOTOR_NEG_PIN,MAX_MOTOR_NEG);
    Serial.print(DIRECTION_NEGATIVE);
    run_mills*=-1;
  }
  Serial.print(" for ");
  Serial.print(run_mills);
  Serial.println(" ms");
  wheel = run_mills;
  unsigned int cur_mills = millis();
  motor_stop_time_mills = cur_mills + run_mills;
}


/*
 * See if the motor is running and needs to stop
 */
void check_motor() {
  unsigned int cur_mills = millis();
  if(motor_stop_time_mills < cur_mills && motor_stop_time_mills) {
    motor_stop_time_mills = 0;
    wheel = 0;
    Serial.println("Stopping motor");
    analogWrite(MOTOR_PLUS_PIN,0);
    analogWrite(MOTOR_NEG_PIN,0);
  }
}

float read_compass() {
  /* Get a new sensor event */
  sensors_event_t event;
  mag.getEvent(&event);
  // Calculate the angle of the vector y,x to get the heading
  float heading = (atan2(event.magnetic.y,event.magnetic.x) * 180) / PI;
  
  // Normalize heading to 0-360
  if (heading < 0){
    heading = 360 + heading;
  }
  Serial.print("Compass Heading: ");
  Serial.println(heading);
  return heading;  
}

/*
 * See if it is time to read the compass, if so adjust the running averages and if necessary the heading adjust;
 */
void check_compass() {
  unsigned int cur_mills = millis();
  if (last_compass_read_time_mills - cur_mills > COMPASS_READ_INTERVAL) {
    float heading = read_compass();
    heading_long_average = heading_long_average + ((heading - heading_long_average) / COMPASS_LONG_AVERAGE_SIZE);
    heading_short_average = heading_short_average + ((heading - heading_short_average) / COMPASS_SHORT_AVERAGE_SIZE);
    heading_long_average_change = heading_long_average_change + (((heading - heading_long_average) - heading_long_average_change) / COMPASS_LONG_AVERAGE_SIZE);
    heading_short_average_change = heading_short_average_change + (((heading - heading_short_average) - heading_short_average_change) / COMPASS_SHORT_AVERAGE_SIZE);
    Serial.print("Average Heading: ");
    Serial.print(heading_long_average);
    Serial.print(" ");
    Serial.print(heading_short_average);
    Serial.print(" ");
    Serial.print(heading_long_average_change);
    Serial.print(" ");
    Serial.println(heading_short_average_change);
    if (strcmp(mode,"compass") == 0) {
      float alter = bearing - heading_short_average;
      if (alter > 180) {
        alter =  alter - 360;
      } else if (alter < -180) {
        alter = alter + 360;
      }
      course_correction = alter;
      Serial.print("Course Correction: ");
      Serial.println(course_correction);
    }
  }
}

/*
 * For now we just use short average heading and don't use heading change, but this is where we would make use
 * of all those to make more intelligent course corrections. 
 */
void check_heading() {
  if (heading_short_average > 1.0 or heading_short_average < -1.0) {
    // 200 is a complete guess, we'll play with this a bunch and make a constant out of it.
    int run_mills = heading_short_average * 200;
    start_motor(run_mills);
  }
}

void check_command() {
  // wait for a new client on command server:
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
}

void check_debug() {
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

void loop() {
  check_motor();
  check_compass();
  check_heading();
  check_command();
  check_debug();
  display();
}

void display() {
  if (millis() - display_refresh_timer > DISPLAY_UPDATE_RATE) {
    display_refresh_timer = millis(); // reset the timer
  int16_t  x1, y1;
  uint16_t w, h;

  // Canvas for Speed 
  static GFXcanvas1 speed_canvas(160, 80);
  speed_canvas.fillScreen(HX8357_BLACK);
  speed_canvas.setFont(&FreeSans9pt7b);

  speed_canvas.drawRect(0,0,160,80,HX8357_WHITE);
  speed_canvas.getTextBounds("Speed", 0, 12, &x1, &y1, &w, &h);
  speed_canvas.fillRect(x1,y1,w+8,h+2, HX8357_WHITE);
  speed_canvas.setCursor(0, 14);
  speed_canvas.setTextColor(HX8357_BLACK);
  speed_canvas.print("Speed");
  speed_canvas.setTextColor(HX8357_WHITE);
  speed_canvas.setFont(&FreeSansBold24pt7b);
  speed_canvas.setCursor(20, 60);
  speed_canvas.print(speed,2);

  tft.drawBitmap(0,0, speed_canvas.getBuffer(),160,80,HX8357_CYAN, HX8357_BLACK);

  // Heading instant, short and long average
  GFXcanvas1 compass_canvas(160, 239);
  compass_canvas.fillScreen(HX8357_BLACK);
  compass_canvas.setFont(&FreeSans9pt7b);

  compass_canvas.drawRect(0,0,160,239,HX8357_YELLOW);
  compass_canvas.getTextBounds("Heading", 0, 12, &x1, &y1, &w, &h);
  compass_canvas.fillRect(x1,y1,w+8,h+1, HX8357_YELLOW);
  compass_canvas.setCursor(0, 12);
  compass_canvas.setTextColor(HX8357_BLACK);
  compass_canvas.print("Heading");
  compass_canvas.setTextColor(HX8357_YELLOW);
  compass_canvas.setFont(&FreeSansBold18pt7b);
  compass_canvas.setCursor(20, 50);
  compass_canvas.print(heading_long_average);
  compass_canvas.setCursor(20, 90);
  compass_canvas.print(" ~");
  compass_canvas.print(heading_long_average_change);
  compass_canvas.drawLine(0, 100, 160, 100, HX8357_YELLOW);
  compass_canvas.setCursor(20, 135);
  compass_canvas.print(heading_short_average);
  compass_canvas.setCursor(20, 175);
  compass_canvas.print(" ~");
  compass_canvas.print(heading_short_average_change);
  compass_canvas.drawLine(0, 185, 160, 185, HX8357_YELLOW);
  compass_canvas.setCursor(20, 220);
  compass_canvas.print(heading);

  tft.drawBitmap(0,81, compass_canvas.getBuffer(),160,239,HX8357_YELLOW, HX8357_BLACK);

  // Canvas for GPS Coordinates lat and long
  // for current and desired location
  GFXcanvas1 destination_canvas(159, 135);
  destination_canvas.fillScreen(HX8357_BLACK);
  destination_canvas.setFont(&FreeSans9pt7b);

  destination_canvas.drawRect(0,0,159,105,HX8357_YELLOW);
  destination_canvas.getTextBounds("Destination", 0, 12, &x1, &y1, &w, &h);
  destination_canvas.fillRect(x1,y1,w+8,h+3, HX8357_YELLOW);
  destination_canvas.setCursor(0, 14);
  destination_canvas.setTextColor(HX8357_BLACK);
  destination_canvas.print("Destination");
  if (mode == "navigate") {
    destination_canvas.setTextColor(HX8357_YELLOW);
    destination_canvas.setFont(&FreeSansBold12pt7b);
    destination_canvas.setCursor(30, 40);
    destination_canvas.print(mode);
    destination_canvas.setFont(&FreeSansBold12pt7b);
    destination_canvas.setCursor(20, 70);
    destination_canvas.print(waypoint_lat,6);
    destination_canvas.setCursor(10, 95);
    destination_canvas.println(waypoint_lon,6);
  } else if (mode == "compass") {
    destination_canvas.setTextColor(HX8357_YELLOW);
    destination_canvas.setFont(&FreeSansBold12pt7b);
    destination_canvas.setCursor(25, 40);
    destination_canvas.print(mode);
    destination_canvas.setFont(&FreeSansBold24pt7b);
    destination_canvas.setCursor(20, 90);
    destination_canvas.print(heading_desired,1);
  } else {
    destination_canvas.setTextColor(HX8357_YELLOW);
    destination_canvas.setFont(&FreeSansBold24pt7b);
    destination_canvas.setCursor(35, 70);
    destination_canvas.print("N/A");
  }

  tft.drawBitmap(161,0, destination_canvas.getBuffer(),159,105,0xF57F, HX8357_BLACK);

  // // Canvas for Course Correction
  static GFXcanvas1 bearing_canvas(159, 163);
  bearing_canvas.fillScreen(HX8357_BLACK);

  bearing_canvas.drawRect(0,0,159,163,HX8357_WHITE);
  bearing_canvas.setFont(&FreeSans9pt7b);
  bearing_canvas.getTextBounds("Bearing", 0, 12, &x1, &y1, &w, &h);
  bearing_canvas.fillRect(x1,y1,w+8,h+3, HX8357_WHITE);
  bearing_canvas.setCursor(0, 14);
  bearing_canvas.setTextColor(HX8357_BLACK);
  bearing_canvas.print("Bearing");
  if (mode == "navigate" || mode == "compass") {
    bearing_canvas.setFont(&FreeSansBold24pt7b);
    bearing_canvas.setTextColor(HX8357_WHITE);
    bearing_canvas.setCursor(20, 70);
    bearing_canvas.print(bearing,1);
    bearing_canvas.setTextColor(HX8357_WHITE);
    bearing_canvas.setFont(&FreeSansBold24pt7b);
    bearing_canvas.setCursor(10, 120);
    bearing_canvas.print(course_correction, 2);
    bearing_canvas.println((course_correction > 0) ? " R" : " L");     
    bearing_canvas.setFont(&FreeSansBold24pt7b);
    bearing_canvas.setCursor(60, 150);
    if (wheel < 0) {
      bearing_canvas.print("<--");
    } else if (wheel > 0) {
      bearing_canvas.print("-->");
    } else {
      bearing_canvas.print("--");
    }
  } else {
    bearing_canvas.setFont(&FreeSansBold24pt7b);
    bearing_canvas.setTextColor(HX8357_WHITE);
    bearing_canvas.setCursor(35, 90);
    bearing_canvas.print("N/A");
  }

  tft.drawBitmap(161,106, bearing_canvas.getBuffer(), 159, 163, 0xFC09, HX8357_BLACK);

  // Canvas for Distance 
  static GFXcanvas1 distance_canvas(159, 80);
  distance_canvas.fillScreen(HX8357_BLACK);
  distance_canvas.setFont(&FreeSans9pt7b);

  distance_canvas.drawRect(0,0,159,80,HX8357_WHITE);
  distance_canvas.getTextBounds("Distance", 0, 12, &x1, &y1, &w, &h);
  distance_canvas.fillRect(x1,y1,w+8,h+3, HX8357_WHITE);
  distance_canvas.setCursor(0, 14);
  distance_canvas.setTextColor(HX8357_BLACK);
  distance_canvas.print("Distance");
  distance_canvas.setTextColor(HX8357_WHITE);
  distance_canvas.setFont(&FreeSansBold24pt7b);
  distance_canvas.setCursor(20, 60);
  distance_canvas.print(distance,2);

  tft.drawBitmap(321,0, distance_canvas.getBuffer(),159,80,HX8357_CYAN, HX8357_BLACK);

  // Canvas for Course and Bearing
  static GFXcanvas1 gps_canvas(159, 190);
  gps_canvas.fillScreen(HX8357_BLACK);

  gps_canvas.drawRect(0,0,159,94,HX8357_WHITE);
  gps_canvas.setFont(&FreeSans9pt7b);
  gps_canvas.getTextBounds("Course", 0, 12, &x1, &y1, &w, &h);
  gps_canvas.fillRect(x1,y1,w+8,h+3, HX8357_WHITE);
  gps_canvas.setCursor(0, 14);
  gps_canvas.setTextColor(HX8357_BLACK);
  gps_canvas.print("Course");
  if (fix) {
    gps_canvas.setTextColor(HX8357_WHITE);
    gps_canvas.setFont(&FreeSansBold24pt7b);
    gps_canvas.setCursor(20, 70);
    gps_canvas.print(course,1);
  }

  gps_canvas.drawRect(0,94,159,94,HX8357_YELLOW);
  gps_canvas.setFont(&FreeSans9pt7b);
  gps_canvas.getTextBounds("Location", 0, 106, &x1, &y1, &w, &h);
  gps_canvas.fillRect(x1,y1,w+8,h+1, HX8357_YELLOW);
  gps_canvas.setCursor(0, 106);
  gps_canvas.setTextColor(HX8357_BLACK);
  gps_canvas.print("Location");
  if (fix) {
    gps_canvas.setTextColor(HX8357_YELLOW);
    gps_canvas.setFont(&FreeSansBold12pt7b);
    gps_canvas.setCursor(20, 140);
    gps_canvas.print(location_lat,6);
    gps_canvas.setCursor(10, 165);
    gps_canvas.println(location_long,6);
  }
  tft.drawBitmap(321,81, gps_canvas.getBuffer(), 159, 190, 0x7FE8, HX8357_BLACK);

 // Canvas for Date and Time
  static GFXcanvas1 date_time_canvas(319, 50);
  date_time_canvas.fillScreen(HX8357_BLACK);
  date_time_canvas.setFont(&FreeSans9pt7b);

  date_time_canvas.drawRect(0,0,319,50,HX8357_WHITE);
  date_time_canvas.getTextBounds("Date &Time", 0, 12, &x1, &y1, &w, &h);
  date_time_canvas.fillRect(x1,y1,w+8,h+3, HX8357_WHITE);
  date_time_canvas.setCursor(0, 14);
  date_time_canvas.setTextColor(HX8357_BLACK);
  date_time_canvas.print("Date & Time");
  date_time_canvas.setTextColor(HX8357_WHITE);
  date_time_canvas.setFont(&FreeSansBold12pt7b);
  date_time_canvas.setCursor(20, 39);
  date_time_canvas.print(day, DEC); 
  date_time_canvas.print('/');
  date_time_canvas.print(month, DEC); 
  date_time_canvas.print("/");
  date_time_canvas.print(year, DEC);
  date_time_canvas.print(" ");
  if (hour < 10) { 
    date_time_canvas.print('0'); 
  }
  date_time_canvas.print(hour, DEC); 
  date_time_canvas.print(':');
  if (minute < 10) { 
    date_time_canvas.print('0'); 
  }
  date_time_canvas.print(minute, DEC); 
  if (fix) {
    if (fixquality == 0) {
      date_time_canvas.print(" n/a");
    } else if (fixquality == 1) {
      date_time_canvas.print(" GPS");
    } else if (fixquality == 2) {
      date_time_canvas.print(" DGPS");
    }
    date_time_canvas.print(" (");
    date_time_canvas.print(satellites);
    date_time_canvas.print(")");
  }

  tft.drawBitmap(161,270, date_time_canvas.getBuffer(),319,50, HX8357_WHITE, HX8357_BLACK);
  }
}

