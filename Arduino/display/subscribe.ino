#include <WiFiNINA.h>

#define DATA_SIZE 300

char buffer[1000];
WiFiUDP udp; // Create a UDP object
unsigned int localPort = 8888;  // local port to listen on

void setup_subscribe() {
  udp.begin(localPort); // Start the UDP server on port 8888 for incoming messages
  buffer[0] = '\0';
}

void check_subscription() {
  char packetBuffer[DATA_SIZE]; // Buffer to hold incoming packets
  int packetSize = udp.parsePacket();
  char leftover[699];

  if (packetSize) {
    DEBUG_PRINT("Received packet: ");
    DEBUG_PRINT(packetSize);
    int len = udp.read(packetBuffer, DATA_SIZE); // Read the packet into the buffer
    if (len > 0) {
      packetBuffer[len] = '\0'; // Null-terminate the received data
      DEBUG_PRINT(" packet Buffer: ");
      DEBUG_PRINT(packetBuffer); // Print received data
    }
    // add what we received to what was left over before
    strcat(buffer, packetBuffer);
    char *start = strchr(buffer, '~') + 1; //find the start of the sentence
    char *end = strchr(start, '$'); // find the end of the sentence
    // copy off the leftover 
    strcpy(leftover, end + 1);
    DEBUG_PRINT(" leftover ");
    DEBUG_PRINT(leftover);
    // put a null at the end of the sentence
    end[0] = '\0';
    // parse the sentence
    autoPilot.parse(start);
    // copy the left over to the buffer for next time
    strcpy(buffer, leftover);
    DEBUG_PRINTLN(" parsed.");
  }
}


