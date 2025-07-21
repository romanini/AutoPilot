#if defined(ARDUINO_ARCH_SAMD)  // Check if the board is based on the SAMD architecture (like Arduino Nano 33 IoT)
#include <WiFiNINA.h>
#define WIFI_LIB_NAME "WiFiNINA"
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
#include <WiFi.h>
#include <WiFiUdp.h>
#define WIFI_LIB_NAME "WiFi"
#else
#error "Unsupported board type. Please use Arduino Nano 33 IoT or Arduino Nano ESP32."
#endif
#include <TimeLib.h>
#include "arduino_secrets.h"

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = "SoberPilot";  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)

int wifi_status = WL_IDLE_STATUS;

void setup_wifi() {
  if (!connect_wifi(20)) {
    autoPilot.setConnected(false);
  }
  // you're connected now, so print out the status:
  print_wifi_status();
}

// Function to ensure WiFi is connected. Returns true if connected, false otherwise.
bool ensure_wifi_connected() {
  return connect_wifi(5);
}

bool connect_wifi(int maxRetries) {
  DEBUG_PRINTLN("Ensuring WiFi connection...");
#if defined(ARDUINO_ARCH_SAMD)
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi already connected.");
    return true;
  }

  DEBUG_PRINTLN("WiFi not connected. Attempting to reconnect...");
  // First, properly disconnect
  WiFi.disconnect();
  delay(500); // Wait for disconnection to complete

  // Attempt to connect for a certain number of retries
  for (int retry = 0; retry < maxRetries; ++retry) {
    DEBUG_PRINT("Attempt ");
    DEBUG_PRINT(retry + 1);
    DEBUG_PRINT(" to connect to SSID: ");
    DEBUG_PRINT(ssid);
    wifi_status = WiFi.begin(ssid, pass);

    // Wait for connection, with a timeout for each attempt
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 10000) { // 10-second timeout for each attempt
      delay(500);
      DEBUG_PRINT(".");
    }

    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN(" Successful");
      print_wifi_status();
      return true;
    } else {
      DEBUG_PRINTLN(" Failed");
      WiFi.disconnect(); // Ensure clean state for next retry
      delay(1000); // Wait a bit before retrying
    }
  }
  DEBUG_PRINTLN("Failed to reconnect to WiFi after several attempts.");
  return false;

#elif defined(ARDUINO_ARCH_ESP32)
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi already connected.");
    return true;
  }

  DEBUG_PRINTLN("WiFi not connected. Attempting to reconnect for ESP32...");
  WiFi.disconnect(true); // Disconnect and optionally turn off radio
  delay(500);

  for (int retry = 0; retry < maxRetries; ++retry) {
    DEBUG_PRINT("Attempting to connect to SSID: ");
    DEBUG_PRINTLN(ssid);
    WiFi.begin(ssid, pass); // For ESP32, begin() is non-blocking in this style

    DEBUG_PRINT("Connection attempt ");
    DEBUG_PRINT(retry + 1);
    DEBUG_PRINTLN("...");

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 10000) { // 10-second timeout for each attempt
      delay(500);
      DEBUG_PRINT(".");
    }
    DEBUG_PRINTLN();

    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("Successfully reconnected to WiFi (ESP32).");
      wifi_status = WL_CONNECTED; // Update global status
      print_wifi_status();
      return true;
    } else {
      DEBUG_PRINT("Failed to connect on attempt ");
      DEBUG_PRINTLN(retry + 1);
      // ESP32 handles disconnect differently, WiFi.disconnect(true) was called earlier.
      // May not need to call disconnect again here unless issues are observed.
      delay(1000);
    }
  }
  DEBUG_PRINTLN("Failed to reconnect to WiFi after several attempts (ESP32).");
  wifi_status = WL_DISCONNECTED; // Update global status
  return false;
#endif
  return false; // Default fallback, should be covered by specific archs
}

void print_wifi_status() {
#if defined(ARDUINO_ARCH_SAMD)
  Serial.print("WiFi firmware version: ");
  Serial.println(WiFi.firmwareVersion());
#endif

  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
}