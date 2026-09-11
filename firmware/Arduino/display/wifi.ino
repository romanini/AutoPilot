#include <WiFi.h>
#define WIFI_LIB_NAME "WiFi"
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
  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINTLN("WiFi already connected.");
    return true;
  }

  DEBUG_PRINTLN("WiFi not connected. Attempting to reconnect...");
  WiFi.disconnect(true);  // disconnect and turn off the radio for a clean start
  delay(500);

  for (int retry = 0; retry < maxRetries; ++retry) {
    DEBUG_PRINT("Attempting to connect to SSID: ");
    DEBUG_PRINTLN(ssid);
    WiFi.begin(ssid, pass);  // non-blocking on ESP32

    DEBUG_PRINT("Connection attempt ");
    DEBUG_PRINT(retry + 1);
    DEBUG_PRINTLN("...");

    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < 10000) {  // 10s timeout per attempt
      delay(500);
      DEBUG_PRINT(".");
    }
    DEBUG_PRINTLN();

    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("Successfully connected to WiFi.");
      wifi_status = WL_CONNECTED;
      print_wifi_status();
      return true;
    } else {
      DEBUG_PRINT("Failed to connect on attempt ");
      DEBUG_PRINTLN(retry + 1);
      delay(1000);
    }
  }
  DEBUG_PRINTLN("Failed to connect to WiFi after several attempts.");
  wifi_status = WL_DISCONNECTED;
  return false;
}

void print_wifi_status() {
  // print the SSID of the network you're attached to:
  DEBUG_PRINT("SSID: ");
  DEBUG_PRINTLN(WiFi.SSID());

  // print your board's IP address:
  IPAddress ip = WiFi.localIP();
  DEBUG_PRINT("IP Address: ");
  DEBUG_PRINTLN(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  DEBUG_PRINT("signal strength (RSSI):");
  DEBUG_PRINT(rssi);
  DEBUG_PRINTLN(" dBm");
}