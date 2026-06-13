
#include <WiFi.h>
#include <AsyncUDP.h>

#define PUBLISH_INTERVAL 1000
#define DATA_SIZE 300
#define BROADCAST_PORT 8888

// Subnet-directed broadcast for the soft-AP's own network. We send here instead
// of AsyncUDP::broadcastTo() (which targets the limited broadcast 255.255.255.255):
// in AP-only mode there is no default route, so 255.255.255.255 has no interface
// to egress and the datagram is silently dropped. 10.20.1.255 matches the soft-AP
// netif's subnet, so lwip routes it out the AP interface as an L2 broadcast that
// every associated station (display, OpenCPN, ...) receives.
IPAddress broadcastIp(10, 20, 1, 255);

AsyncUDP udpClient;

uint32_t last_publish_time_mills = millis();
char serialzied_data[DATA_SIZE];

void setup_publish() {
  DEBUG_PRINTLN("Publishing all setup");
}

void publish_APDAT() {
  if (millis() - last_publish_time_mills > PUBLISH_INTERVAL) {
    last_publish_time_mills = millis();
    time_t currentTime = autoPilot.getDateTime();
    sprintf(serialzied_data, "~APDAT,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%f,%f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.6f,%.6f$",
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

            autoPilot.getLocationLat(),  //%.6f
            autoPilot.getLocationLon()   //%.6f
    );

    //DEBUG_PRINTLN(serialzied_data);

    udpClient.writeTo((const uint8_t *)serialzied_data, strlen(serialzied_data), broadcastIp, BROADCAST_PORT);
  }
}

void publish_RESET() {
  sprintf(serialzied_data, "~RESET,1$");
  DEBUG_PRINTLN(serialzied_data);
  udpClient.writeTo((const uint8_t *)serialzied_data, strlen(serialzied_data), broadcastIp, BROADCAST_PORT);
}