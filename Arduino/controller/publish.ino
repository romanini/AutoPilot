#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  #include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  #include <WiFi.h>
#include <AsyncUDP.h>
#else
  #error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

#define PUBLISH_INTERVAL 1000
#define DATA_SIZE 300
#define BROADCAST_PORT 8888

//IPAddress broadcastIp(10,20,1,255);


#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
WiFiUDP udpClient; // Create a UDP object
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
AsyncUDP udpClient;
#endif

uint32_t last_publish_time_mills = millis();
char serialzied_data[DATA_SIZE];

void setup_publish() {
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  udpClient.begin(BROADCAST_PORT);
#endif  
  Serial.println("Publishing all setup");
}

void publish_APDAT() {
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

    //DEBUG_PRINTLN(serialzied_data);
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
    udpClient.beginPacket(broadcastIp, BROADCAST_PORT);
    udpClient.write(serialzied_data);
    udpClient.endPacket();
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
    udpClient.broadcastTo(serialzied_data, BROADCAST_PORT);
#endif
    flash_led();
  }
}

void publish_RESET() {
    sprintf(serialzied_data, "~RESET,1$");
    DEBUG_PRINTLN(serialzied_data);
#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
    udpClient.beginPacket(broadcastIp, BROADCAST_PORT);
    udpClient.write(serialzied_data);
    udpClient.endPacket();
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
    udpClient.broadcastTo(serialzied_data, BROADCAST_PORT);
#endif
}