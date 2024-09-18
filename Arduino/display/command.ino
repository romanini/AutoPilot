#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif

//#define MOCK_SEND false
#define COMMAND_LEN 12
#define TIMEOUT_MS 1000  // Set your desired timeout value in milliseconds
#define COMMAND_PORT 8023
#define COMMAND_DELAY 200

IPAddress processor_ip(10, 20, 1, 1);
WiFiClient client;

char command[COMMAND_LEN];

boolean connect() {
  if (client.connected()) {
    client.stop();
  }
  if (!client.connect(processor_ip, COMMAND_PORT)) {
    Serial.println("Could not connect to processor.");
    return false;
  }
  return true;
}

void send_command(const char* command) {
  if (connect()) {
    client.write('\0');
    client.print(command);
    client.write('\n');
    client.flush();
    delay(COMMAND_DELAY);
  }
}

void setup_command() {
  // Set the timeout for WiFiClient
  client.setTimeout(TIMEOUT_MS);
}

void check_command() {
  if (autoPilot.getReset()) {
    autoPilot.setReset(false);
  }
}

void adjust_heading(float change) {
#ifndef MOCK_SEND
  command[0] = '/0';
  sprintf(command, "a%.2f", change);
  send_command(command);
  DEBUG_PRINT("adjusting heading ");
  DEBUG_PRINTLN(change);
#endif
}

void set_mode(int mode) {
#ifndef MOCK_SEND
  command[0] = '/0';
  sprintf(command, "m%d", mode);
  send_command(command);
  DEBUG_PRINT("set mode ");
  DEBUG_PRINTLN(mode);

#endif
}


