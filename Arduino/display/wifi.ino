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
  Serial.print("Using ");
  Serial.println(WIFI_LIB_NAME);

#if defined(ARDUINO_ARCH_SAMD)
  // check for the WiFi module:
  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    // don't continue
    while (true)
      ;
  }

  String fv = WiFi.firmwareVersion();
  if (fv < WIFI_FIRMWARE_LATEST_VERSION) {
    Serial.println("Please upgrade the firmware");
  }

  // attempt to connect to WiFi network:
  while (wifi_status != WL_CONNECTED) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    wifi_status = WiFi.begin(ssid, pass);
    // wait 10 seconds for connection:
    delay(5000);
  }
#elif defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  wifi_status = WiFi.begin(ssid, pass);

  // attempt to connect to WiFi network:
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);

  }
#endif
  // you're connected now, so print out the status:
  print_wifi_status();
}

// Function to ensure WiFi is connected. Returns true if connected, false otherwise.
bool ensure_wifi_connected() {
  Serial.println("Ensuring WiFi connection...");
#if defined(ARDUINO_ARCH_SAMD)
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi already connected.");
    return true;
  }

  Serial.println("WiFi not connected. Attempting to reconnect...");
  // First, properly disconnect
  WiFi.disconnect();
  delay(500); // Wait for disconnection to complete

  // Attempt to connect for a certain number of retries
  const int maxRetries = 5; // Or a time-based approach
  for (int retry = 0; retry < maxRetries; ++retry) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    wifi_status = WiFi.begin(ssid, pass);

    Serial.print("Connection attempt ");
    Serial.print(retry + 1);
    Serial.println("...");

    // Wait for connection, with a timeout for each attempt
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 10000) { // 10-second timeout for each attempt
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Successfully reconnected to WiFi.");
      print_wifi_status();
      return true;
    } else {
      Serial.print("Failed to connect on attempt ");
      Serial.println(retry + 1);
      WiFi.disconnect(); // Ensure clean state for next retry
      delay(1000); // Wait a bit before retrying
    }
  }
  Serial.println("Failed to reconnect to WiFi after several attempts.");
  return false;

#elif defined(ARDUINO_ARCH_ESP32)
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi already connected.");
    return true;
  }

  Serial.println("WiFi not connected. Attempting to reconnect for ESP32...");
  WiFi.disconnect(true); // Disconnect and optionally turn off radio
  delay(500);

  const int maxRetries = 5;
  for (int retry = 0; retry < maxRetries; ++retry) {
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    WiFi.begin(ssid, pass); // For ESP32, begin() is non-blocking in this style

    Serial.print("Connection attempt ");
    Serial.print(retry + 1);
    Serial.println("...");

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 10000) { // 10-second timeout for each attempt
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Successfully reconnected to WiFi (ESP32).");
      wifi_status = WL_CONNECTED; // Update global status
      print_wifi_status();
      return true;
    } else {
      Serial.print("Failed to connect on attempt ");
      Serial.println(retry + 1);
      // ESP32 handles disconnect differently, WiFi.disconnect(true) was called earlier.
      // May not need to call disconnect again here unless issues are observed.
      delay(1000);
    }
  }
  Serial.println("Failed to reconnect to WiFi after several attempts (ESP32).");
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