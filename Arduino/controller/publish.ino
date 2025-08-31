
#include <WiFi.h>
#include <AsyncUDP.h>

#define PUBLISH_INTERVAL 1000
#define DATA_SIZE 300
#define BROADCAST_PORT 8888

//IPAddress broadcastIp(10,20,1,255);

AsyncUDP udpClient;

uint32_t last_publish_time_mills = millis();
char serialzied_data[DATA_SIZE];

void setup_publish() {
  Serial.println("Publishing all setup");
}

void publish_APDAT() {
  if (millis() - last_publish_time_mills > PUBLISH_INTERVAL) {
    last_publish_time_mills = millis();
    time_t currentTime = autoPilot.getDateTime();
    sprintf(serialzied_data, "~APDAT,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%f,%f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f$",
            year(currentTime) % 100,    // %d
            month(currentTime),         //%d
            day(currentTime),           //%d
            hour(currentTime),          // %d
            minute(currentTime),        //%d
            autoPilot.hasFix(),         // %d
            autoPilot.getFixquality(),  // %d
            autoPilot.getSatellites(),  // %d

            autoPilot.isNavigationEndabled(), // %d
            autoPilot.getMode(),         // %d
            autoPilot.isWaypointSet(),   // %d
            autoPilot.getWaypointLat(),  // %f
            autoPilot.getWaypointLon(),  // %f

            autoPilot.getHeadingDesired(),            //%.2f

            autoPilot.getHeading(),            //%.2f
            autoPilot.getPitch(),             // %.2f
            autoPilot.getRoll(),              // %.2f
            autoPilot.getStabilityClassification(), // %d
            autoPilot.getBearing(),            //%.2f
            autoPilot.getBearingCorrection(),  //%.2f

            autoPilot.getSpeed(),     // %.2f
            autoPilot.getDistance(),  // %.2f
            autoPilot.getCourse(),    //%.2f

            autoPilot.getLocationLat(),  //%.2f
            autoPilot.getLocationLon()   //%.2f
    );

    //DEBUG_PRINTLN(serialzied_data);

    udpClient.broadcastTo(serialzied_data, BROADCAST_PORT);
    flash_led();
  }
}

void publish_RESET() {
  sprintf(serialzied_data, "~RESET,1$");
  DEBUG_PRINTLN(serialzied_data);
  udpClient.broadcastTo(serialzied_data, BROADCAST_PORT);
}