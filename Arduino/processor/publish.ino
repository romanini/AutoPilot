#include <WiFiNINA.h>

#define PUBLISH_INTERVAL 1000
#define DATA_SIZE 300
#define BROADCAST_PORT 8888

IPAddress broadcastIp(10,20,1,255);
unsigned int broadcastPort = 8888;

WiFiUDP udpClient; // Create a UDP object

uint32_t last_publish_time_mills = millis();
char serialzied_data[DATA_SIZE];

void setup_publish() {
  udpClient.begin(broadcastPort);
}

void publish() {
  if (millis() - last_publish_time_mills > PUBLISH_INTERVAL) {
    last_publish_time_mills = millis();
    time_t currentTime = autoPilot.getDateTime();
    sprintf(serialzied_data, "~APDAT,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%f,%f,%.2f,%.2f,%.2f,%d,%.2f,%.2f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f$",
      year(currentTime) % 100,     // %d
      month(currentTime),       //%d
      day(currentTime), //%d 
      hour(currentTime),     // %d
      minute(currentTime),    //%d
      autoPilot.hasFix(),     // %d
      autoPilot.getFixquality(),  // %d
      autoPilot.getSatellites(),   // %d

      autoPilot.getMode(), // %d
      autoPilot.isWaypointSet(), // %d
      autoPilot.getWaypointLat(),  // %f
      autoPilot.getWaypointLon(),  // %f 

      autoPilot.getHeadingDesired(),  //%.2f
      autoPilot.getHeadingLongAverage(), //%.2f
      autoPilot.getHeadingLongAverageChange(), // %.2f
      autoPilot.getHeadingLongAverageSize(), //%d

      autoPilot.getHeadingShortAverage(), //%.2f
      autoPilot.getHeadingShortAverageChange(), //%.2f
      autoPilot.getHeadingShortAverageSize(),  //%d

      autoPilot.getHeading(), //%.2f
      autoPilot.getBearing(), //%.2f
      autoPilot.getBearingCorrection(),  //%.2f

      autoPilot.getSpeed(), // %.2f
      autoPilot.getDistance(), // %.2f
      autoPilot.getCourse(),  //%.2f
      
      autoPilot.getLocationLat(), //%.2f 
      autoPilot.getLocationLon() //%.2f
    );

    DEBUG_PRINTLN(serialzied_data);
    udpClient.beginPacket(broadcastIp, broadcastPort);
    udpClient.write(serialzied_data);
    udpClient.endPacket();
  }
}