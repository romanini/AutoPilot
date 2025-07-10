#include <sys/_intsup.h>
//#include "api/HardwareSerial.h"
#ifndef AUTOPILOT_H
#define AUTOPILOT_H

#if defined(ARDUINO_ARCH_SAMD)  // For Arduino Nano 33 IoT
  #include <Arduino.h>
  typedef HardwareSerial SerialType;  // Define SerialType for SAMD
#elif defined(ARDUINO_ARCH_ESP32)  // For Arduino Nano ESP32
  #include <USB.h>
  typedef USBCDC SerialType;  // Define SerialType for ESP32
#else
  #error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

#include <TimeLib.h> // Include the Time library if needed

#define MAXLINELENGTH 300 ///< how long are max NMEA lines to parse?
#define NMEA_MAX_SENTENCE_ID 20 ///< maximum length of a sentence ID name, including terminating 0
#define NMEA_MAX_SOURCE_ID 3 ///< maximum length of a source ID name, including terminating 0

#define APDAT "APDAT,"
#define RESET "RESET,"

#define STABILITY_CLASSIFIER_UNKNOWN (0)
#define STABILITY_CLASSIFIER_ON_TABLE (1)
#define STABILITY_CLASSIFIER_STATIONARY (2)
#define STABILITY_CLASSIFIER_STABLE (3)
#define STABILITY_CLASSIFIER_MOTION (4)

class AutoPilot {
private:
  int year;
  int month;
  int day;
  int hour;
  int minute;

  bool fix;         // indicator if GPS has satellite fix
  int fixquality;  // the quality of the GPS fix 1 = GPS, 2=DGPS
  int satellites;   // number of satellites fixes by GPS

  bool navigation_enabled;
  int mode;                  // 0 = off, 1 = compass, 2 = navigate

  bool waypoint_set;    // flag indicating if the waypoint has been set
  float waypoint_lat;   // desired waypoint latitide
  float waypoint_lon;   // desired waypoint longitude

  float heading_desired;     // desired heading if navigating by comapss

  float heading;                       // direction of the bow is pointing at the moment (changes frequently)
  float pitch;
  float roll;  
  int stability_classification;
  float bearing;             // desired direction of travel use in both modes
  float bearing_correction;  // correction needed to return to proper bearing
  float speed;          // speed of travel according to GPS
  float distance;       // distance to desired waypoint from current location according to GPS
  float course;         // compass course towads desired waypoint

  float location_lat;   // current latitude
  float location_lon;  // current longitude
  bool destinationChanged;
  unsigned long tackRequested;
  float battery_voltage;
  int battery_voltage_average_size;
  float input_voltage;
  int input_voltage_average_size;
  
  bool reset;
  bool connected;

  bool isEmpty(char *ptart);
  SerialType *serial;
  void parseAPDAT(char *buffer);
  void parseRESET(char *buffer);

public:
  AutoPilot(SerialType* ser);
  void init();
  int getYear();
  int getMonth();
  int getDay();
  int getHour();
  int getMinute();
  bool hasFix();
  int getFixquality();
  int getSatellites();
  int getMode();
  void setMode(int mode);
  bool isNavigationEnabled();
  void setNavigationEnabled(bool nav);
  bool isWaypointSet();
  float getWaypointLat();
  float getWaypointLon();
  float getHeadingDesired();
  float getHeading();  
  float getPitch();
  float getRoll();  
  int getStabilityClassification();
  float getBearing();
  float getBearingCorrection();
  float getSpeed();
  float getDistance();
  float getCourse();
  float getLocationLat();
  float getLocationLon();
  bool hasDestinationChanged();
  float getBatteryVoltage();
  void setBatteryVoltage(float voltage);
  float getInputVoltage();
  void setInputVoltage(float voltage);
  bool getReset();
  void setReset(bool val);
  bool isConnected();
  void setConnected(bool connected);
  void printAutoPilot();
  void parse(char *buffer);
  unsigned long getTackRequested();
  void setTackRequested(unsigned long time);
  void resetTackRequested();
  bool isTackRequested();
};

#endif