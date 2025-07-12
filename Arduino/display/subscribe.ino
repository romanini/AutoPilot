#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

#define DATA_SIZE 300
#define LAST_RECEIVE_MAX_TIME 10000
#define RECONNECT_INTERVAL 30000

char buffer[1000];
WiFiUDP udp;                    // Create a UDP object
unsigned int localPort = 8888;  // local port to listen on
unsigned long lastReceiveTime = 0;
unsigned long lastConnect = 0;
bool receiveTimeout = true;

void setup_subscribe() {
  udp.begin(localPort);  // Start the UDP server on port 8888 for incoming messages
  buffer[0] = '\0';
  lastConnect = millis();
}

void check_subscription() {
  char packetBuffer[DATA_SIZE];  // Buffer to hold incoming packets
  int packetSize = udp.parsePacket();
  char leftover[699];

  if (!receiveTimeout && (lastReceiveTime < millis() - LAST_RECEIVE_MAX_TIME)) {
    receiveTimeout = true;
    autoPilot.init();
    autoPilot.setConnected(false);
  }
  if (receiveTimeout && (lastConnect < millis() - RECONNECT_INTERVAL )) {
    DEBUG_PRINTLN("Attempting to reconnect");
    setup_wifi();
    udp.stop();
    setup_subscribe();
  }
  if (packetSize) {
    //DEBUG_PRINT("Received packet: ");
    //DEBUG_PRINT(packetSize);
    int len = udp.read(packetBuffer, DATA_SIZE);  // Read the packet into the buffer
    if (len > 0) {
      packetBuffer[len] = '\0';  // Null-terminate the received data
      //DEBUG_PRINT(" packet Buffer: ");
      //DEBUG_PRINT(packetBuffer); // Print received data
    }
    // add what we received to what was left over before
    strcat(buffer, packetBuffer);
    char *start = strchr(buffer, '~');  //find the start of the sentence
    if (start != NULL) {
      start++;                         //move past the ~
      char *end = strchr(start, '$');  // find the end of the sentence
      if (end != NULL) {
        // copy off the leftover
        strcpy(leftover, end + 1);
        //DEBUG_PRINT(" leftover ");
        //DEBUG_PRINT(leftover);
        // put a null at the end of the sentence
        end[0] = '\0';
        // parse the sentence
        autoPilot.parse(start);
        lastReceiveTime = millis();
        receiveTimeout = false;
#if DEBUG_ENABLED
        //flash_receive_led();
#endif
        // copy the left over to the buffer for next time
        strcpy(buffer, leftover);
        //DEBUG_PRINTLN(" parsed.");
      }
    } else {
      // if there is no start no sense in keeping it, throw it away and hope we get a start next time.
      buffer[0] = '\0';
    }
  }
}
