#include "api/HardwareSerial.h"
#ifndef AUTOPILOT_H
#define AUTOPILOT_H

#include "Arduino.h"
#include <TimeLib.h> // Include the Time library if needed

#define MAXLINELENGTH 300 ///< how long are max NMEA lines to parse?
#define NMEA_MAX_SENTENCE_ID 20 ///< maximum length of a sentence ID name, including terminating 0
#define NMEA_MAX_SOURCE_ID 3 ///< maximum length of a source ID name, including terminating 0

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

  int mode;                  // 0 = off, 1 = compass, 2 = navigate

  bool waypoint_set;    // flag indicating if the waypoint has been set
  float waypoint_lat;   // desired waypoint latitide
  float waypoint_lon;   // desired waypoint longitude

  float heading_desired;     // desired heading if navigating by comapss

  float heading_long_average;          // long running average forr the heading (most stable)
  float heading_long_average_change;   // rage of change for the long average
  int heading_long_average_size;
  float heading_short_average_change;  // rate of change for the short average
  float heading_short_average;         // short running average for the heading (more stable)
  int heading_short_average_size;

  float heading;                       // direction of the bow is pointing at the moment (changes frequently)
  float bearing;             // desired direction of travel use in both modes
  float bearing_correction;  // correction needed to return to proper bearing
  float speed;          // speed of travel according to GPS
  float distance;       // distance to desired waypoint from current location according to GPS
  float course;         // compass course towads desired waypoint

  float location_lat;   // current latitude
  float location_lon;  // current longitude
  bool modeChanged;
  bool destinationChanged;
  float battery_voltage;
  int battery_voltage_average_size;
  float input_voltage;
  int input_voltage_average_size;
  
  bool isEmpty(char *ptart);
  HardwareSerial *serial;

public:
  AutoPilot(HardwareSerial* ser);
  int getYear();
  int getMonth();
  int getDay();
  int getHour();
  int getMinute();
  bool hasFix();
  int getFixquality();
  int getSatellites();
  int getMode();
  bool isWaypointSet();
  float getWaypointLat();
  float getWaypointLon();
  float getHeadingDesired();
  float getHeadingLongAverage();
  float getHeadingLongAverageChange();  
  int getHeadingLongAverageSize();
  float getHeadingShortAverage();
  float getHeadingShortAverageChange();  
  int getHeadingShortAverageSize();
  float getHeading();  
  float getBearing();
  float getBearingCorrection();
  float getSpeed();
  float getDistance();
  float getCourse();
  float getLocationLat();
  float getLocationLon();
  bool hasDestinationChanged();
  bool hasModeChanged();
  float getBatteryVoltage();
  void setBatteryVoltage(float voltage);
  float getInputVoltage();
  void setInputVoltage(float voltage);
  void printAutoPilot();
  void parse(char *buffer);
};

#endif