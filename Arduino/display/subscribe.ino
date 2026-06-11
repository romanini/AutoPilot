#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

// Forward declaration for the function in wifi.ino
bool ensure_wifi_connected();

#define DATA_SIZE 300
#define LAST_RECEIVE_MAX_TIME 10000
#define RECONNECT_INTERVAL 30000
#define RECONNECT_WAIT_INTERVAL 30000


char buffer[1000];
WiFiUDP udp;                    // Create a UDP object
unsigned int localPort = 8888;  // local port to listen on
unsigned long lastReceiveTime = 0;
unsigned long lastConnect = 0;
unsigned long disconnect_time = 0;

bool receiveTimeout = true;

void setup_subscribe() {
  udp.begin(localPort);  // Start the UDP server on port 8888 for incoming messages
  buffer[0] = '\0';
  lastConnect = millis();
}

void check_subscription() {
  unsigned long now = millis();

  if (!receiveTimeout && (lastReceiveTime < now - LAST_RECEIVE_MAX_TIME)) {
    disconnect_time = millis();
    receiveTimeout = true;
    autoPilot.init();
    DEBUG_PRINT("No UDP receives in more than ");
    DEBUG_PRINT(LAST_RECEIVE_MAX_TIME);
    DEBUG_PRINTLN("ms setting receive timeout");
  }
  // if we are disconnected we want to wait a bit before trying to reconnect
  // also  when reconnecting we want to wait between retry attempts
  if (receiveTimeout && (disconnect_time < now - RECONNECT_WAIT_INTERVAL) && (lastConnect < now - RECONNECT_INTERVAL)) {
    DEBUG_PRINT("Attempting to restore connection...");
    lastConnect = now;  // Update lastConnect time immediately to prevent rapid retries

    if (ensure_wifi_connected()) {
      DEBUG_PRINTLN("WiFi connection confirmed/re-established. Resetting UDP.");
      setup_subscribe();  // This calls udp.begin()
      // Optionally, reset receiveTimeout to give UDP a fresh chance immediately,
      // or let it be set by the normal packet handling logic.
      receiveTimeout = false;
      lastReceiveTime = millis();  // If resetting receiveTimeout
    } else {
      DEBUG_PRINTLN("Failed to re-establish WiFi connection. Will retry later.");
      // No need to try UDP setup if WiFi is down.
    }
  }
  if (!receiveTimeout) {
    char packetBuffer[DATA_SIZE];  // Buffer to hold incoming packets
    int packetSize = udp.parsePacket();
    char leftover[sizeof(buffer)];
    if (packetSize) {
      //DEBUG_PRINT("Received packet: ");
      //DEBUG_PRINT(packetSize);
      // Read at most DATA_SIZE - 1 so there is always room for the NUL
      // terminator written below (avoids an off-by-one write at packetBuffer[DATA_SIZE]).
      int len = udp.read(packetBuffer, DATA_SIZE - 1);  // Read the packet into the buffer
      if (len < 0) {
        len = 0;
      }
      packetBuffer[len] = '\0';  // Null-terminate the received data
      //DEBUG_PRINT(" packet Buffer: ");
      //DEBUG_PRINT(packetBuffer); // Print received data

      // add what we received to what was left over before, but never overflow
      // buffer. If it won't fit, the accumulated data is malformed (we never
      // saw a terminating '$'), so drop it and resync on the next '~'.
      if (strlen(buffer) + (size_t)len < sizeof(buffer)) {
        strcat(buffer, packetBuffer);
      } else {
        buffer[0] = '\0';
      }
      char *start = strchr(buffer, '~');  //find the start of the sentence
      if (start != NULL) {
        start++;                         //move past the ~
        char *end = strchr(start, '$');  // find the end of the sentence
        if (end != NULL) {
          // copy off the leftover (bounded so we can't overrun leftover[])
          strncpy(leftover, end + 1, sizeof(leftover) - 1);
          leftover[sizeof(leftover) - 1] = '\0';
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
}
