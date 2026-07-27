#include <sys/_intsup.h>
#ifndef AUTOPILOT_H
#define AUTOPILOT_H

#include <USB.h>
typedef USBCDC SerialType;  // USB CDC serial on the Arduino Nano ESP32

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

#define LOCAL_COMMAND_SUPPRESS_MS 2250

class AutoPilot {
private:
  SemaphoreHandle_t mutex;
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
  int nav_source;            // who's steering, from APDAT: 0=NONE, 1=GARMIN, 2=OPENCPN

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

  float cog_damped;         // trust-gated, vector-averaged GPS track (controller's gpstracktrim.ino) - stable, but only meaningful while valid below
  bool  cog_damped_valid;    // true once cog_damped has a real (speed-trusted) sample; false near-zero speed or before the first one

  float location_lat;   // current latitude
  float location_lon;  // current longitude
  bool destinationChanged;
  bool modeChanged;
  unsigned long tackRequested;
  int autoTuneState;              // 0 = idle, 1 = ready (armed), 2 = running - mirrors the controller's copy
  unsigned long autoTuneReadyAt;  // local millis() timestamp, for the display's own 30s "ready" timeout
  float battery_voltage;
  int battery_voltage_average_size;
  float input_voltage;
  int input_voltage_average_size;
  
  bool reset;
  bool connected;

  unsigned long localCommandTime;

  bool isEmpty(char *ptart);
  float getCourseCorrection(float bearing, float course);
  float toRadians(float degrees);
  float toDegrees(float radians);
  float getBearing(float lat1, float lon1, float lat2, float lon2);
  SerialType *serial;
  void parseAPDAT(char *buffer);
  void parseRESET(char *buffer);
  float normalizeDegrees(float degrees);
  void lock();
  void unlock();
public:
  AutoPilot(SerialType* ser);
  ~AutoPilot();
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
  int getNavSource();
  bool isNavigationEnabled();
  void setNavigationEnabled(bool nav);
  bool isWaypointSet();
  float getWaypointLat();
  float getWaypointLon();
  float getHeadingDesired();
  void adjustHeadingDesired(float change);
  float getHeading();  
  float getPitch();
  float getRoll();  
  int getStabilityClassification();
  float getBearing();
  float getBearingCorrection();
  float getSpeed();
  float getDistance();
  float getCourse();
  float getDampedCourse();
  bool  isDampedCourseValid();
  float getLocationLat();
  float getLocationLon();
  bool hasDestinationChanged();
  bool hasModeChanged();
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
  void cancelTackRequested();
  bool isTackRequested();
  int getAutoTuneState();
  unsigned long getAutoTuneReadyAt();
  void armAutoTune();
  void startAutoTune();
  void cancelAutoTune();
};

#endif