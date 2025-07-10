#include <sys/_timeval.h>

#ifndef AUTOPILOT_H
#define AUTOPILOT_H

#include <USB.h>
#include <FreeRTOS.h>
typedef USBCDC SerialType;  // Define SerialType for ESP32

#include <TimeLib.h>  // Include the Time library if needed

struct euler_t {
  float yaw;
  float pitch;
  float roll;
};

class AutoPilot {
private:
  SemaphoreHandle_t mutex;
  time_t dateTime;  // current time and date in local TZ

  int motor_stop_time;      // time at which to stop the motor in milliseconds
  int motor_direction;      // -1 = port (L), 0 = not running, 1 = starbord (R)
  int motor_last_run_time;  // time when the last trun happened, this is needed to allow us to wait before making another turn

  bool fix;        // indicator if GPS has satellite fix
  int fixquality;  // the quality of the GPS fix 1 = GPS, 2=DGPS
  int satellites;  // number of satellites fixes by GPS

  bool navigation_enabled;     // indicates navigation is enabled.  Used for when navigating and we want to disable navigation temporeraly and then return to the previous navigation.
  int mode;                  // 0 = off, 1 = compass, 2 = navigate
  float heading_desired;     // desired heading if navigating by comapss
  float bearing;             // desired direction of travel use in both modes
  float bearing_correction;  // correction needed to return to proper bearing

  float heading;                       // direction of the bow is pointing at the moment (changes frequently)
  float pitch;
  float roll;
  int stability_classification;
  int start_motor;
  bool motor_started;

  bool waypoint_set;   // flag indicating if the waypoint has been set
  float waypoint_lat;  // desired waypoint latitide
  float waypoint_lon;  // desired waypoint longitude
  float location_lat;  // current latitude
  float location_lon;  // current longitude
  float course;        // compass course towads desired waypoint
  float speed;         // speed of travel according to GPS
  float distance;      // distance to desired waypoint from current location according to GPS

  bool destinationChanged;  // flag indicating if the destination (waypoint/heading desired) has changed
  bool modeChanged;         // flag indicating that the mode has changed
  float steer_angle;

  SerialType* serial;

  float toRadians(float degrees);
  float toDegrees(float radians);
  float getCourseCorrection(float bearing, float course);
  float getDistance(float lat1, float lon1, float lat2, float lon2);
  float getBearing(float lat1, float lon1, float lat2, float lon2);
  void lock();
  void unlock();
public:
  AutoPilot(SerialType* ser);
  ~AutoPilot();
  void setStartMotor(int start_motor);
  int getStartMotor();
  void setMotorStarted(bool motor_started);
  bool getMotorStarted();

  void setDateTime(time_t dateTime);
  time_t getDateTime();
  int getMotorStopTime();
  int getMotorDirection();
  void setMotor(int motor_stop_time, int motor_direction);
  int getMotorLastRunTime();
  void setMotorLastRunTime(int motor_last_run_time);
  bool hasFix();
  void setFix(bool fix);
  int getFixquality();
  void setFixquality(int fixquality, int satellites);
  int getSatellites();
  int getMode();
  int setMode(int mode);
  bool isNavigationEndabled();
  void setNavigationEnabled(bool enable);
  float getHeadingDesired();
  void adjustHeadingDesired(float change);
  float getBearing();
  float getBearingCorrection();
  float getHeading();
  void setHeading(float heading);
  float getPitch();
  void setPitch(float pitch);
  float getRoll();
  void setStabilityClassification(int value);
  int getStabilityClassification();
  void setRoll(float roll);  
  bool isWaypointSet();
  float getWaypointLat();
  float getWaypointLon();
  void setWaypoint(float lat, float lon);
  float getLocationLat();
  float getLocationLon();
  void setLoation(float lat, float lon, float course);
  float getCourse();
  float getSpeed();
  void setSpeed(float speed);
  float getDistance();
  float getSteerAngle();
  void setSteerAngle(float steer_angle);
  void printAutoPilot();
};

#endif