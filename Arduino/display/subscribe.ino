#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
  #include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  #include <WiFi.h>
  #include <WiFiUdp.h>
#else
  #error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

// Reduced buffer sizes to prevent stack overflow
#define DATA_SIZE 300
#define BUFFER_SIZE 1000  
#define LAST_RECEIVE_MAX_TIME 10000

char buffer[BUFFER_SIZE];
WiFiUDP udp; // Create a UDP object
unsigned int localPort = 8888;  // local port to listen on
unsigned long lastReceiveTime = 0;
bool receiveTimeout = true;

void setup_subscribe() {
  udp.begin(localPort); // Start the UDP server on port 8888 for incoming messages
  buffer[0] = '\0';
}

void check_subscription() {
  char packetBuffer[DATA_SIZE]; // Buffer to hold incoming packets
  int packetSize = udp.parsePacket();
  
  if (!receiveTimeout && (lastReceiveTime < millis() - LAST_RECEIVE_MAX_TIME)) {
    receiveTimeout = true;
    autoPilot.init();
    autoPilot.setMode(-1);
  }
  if (packetSize) {
    //DEBUG_PRINT("Received packet: ");
    //DEBUG_PRINT(packetSize);
    int len = udp.read(packetBuffer, DATA_SIZE); // Read the packet into the buffer
    if (len > 0) {
      packetBuffer[len] = '\0'; // Null-terminate the received data
      //DEBUG_PRINT(" packet Buffer: ");
      //DEBUG_PRINT(packetBuffer); // Print received data
    }

    // Check if adding the packet would overflow the buffer
    if (strlen(buffer) + strlen(packetBuffer) >= BUFFER_SIZE - 1) {
      // Buffer would overflow, reset it
      DEBUG_PRINTLN("Buffer overflow prevented, resetting buffer");
      buffer[0] = '\0';
    }

    // Add what we received to what was left over before
    strcat(buffer, packetBuffer);

    // Find the start and end markers
    char *start = strchr(buffer, '~');
    if (start != NULL) {
      start++; // Move past the '~'
      char *end = strchr(start, '$');

      if (end != NULL) {
        // Process the complete message

        // Create a temporary buffer for leftover data
        size_t leftoverLen = strlen(end + 1);

        // Put a null at the end of the sentence
        end[0] = '\0';

        // Parse the sentence
        autoPilot.parse(start);
        lastReceiveTime = millis();
        receiveTimeout = false;
#if DEBUG_ENABLED
        //flash_receive_led();
#endif

        // Handle leftover data safely
        if (leftoverLen > 0) {
          // Move the leftover data to the beginning of the buffer
          memmove(buffer, end + 1, leftoverLen + 1); // +1 for null terminator
        } else {
          // No leftover data
          buffer[0] = '\0';
        }
      }
    }
    //DEBUG_PRINTLN(" parsed.");
  }
}


