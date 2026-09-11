
#include <WiFi.h>
#include <WiFiUdp.h>
#define WIFI_LIB_NAME "WiFi"

#include <TimeLib.h>
#include "arduino_secrets.h"

///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = "SoberPilot";  // your network SSID (name)
char pass[] = SECRET_PASS;  // your network password (use for WPA, or use as key for WEP)
IPAddress ip(10, 20, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress gateway(10, 20, 1, 1);
IPAddress dns(10, 20, 1, 1);

//int wifi_status = WL_IDLE_STATUS;
bool wifi_status = false;
void setup_wifi() {
  // check for the WiFi module:
  DEBUG_PRINT("Using ");
  DEBUG_PRINTLN(WIFI_LIB_NAME);

  // AP-only: the controller IS the access point - the display, the laptop/telnet,
  // and (eventually) OpenCPN all connect TO it. There is no station side to run.
  // The old WIFI_MODE_APSTA + WiFi.begin(ssid,pass) told the STA to connect to our
  // own soft-AP, which can never associate, so it scanned all channels forever.
  // In APSTA those background scans drag the single radio off the AP channel,
  // stalling AP clients (the multi-second telnet write hangs we saw).
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);  // no modem power-save - keep telnet/telemetry latency low

  WiFi.softAPConfig(ip, gateway, subnet);

  // start the Soft‑AP
  DEBUG_PRINT("Starting SoftAP ...");
  if (!WiFi.softAP(ssid, pass)) {
    DEBUG_PRINTLN(" failed");
    while(true) { delay(1000); }
  }
  DEBUG_PRINTLN(WiFi.softAPSSID());
  wifi_status = true;

  print_wifi_status();
}

void print_wifi_status() {

  // print the SSID of the network you're attached to:
  DEBUG_PRINT("SSID: ");
  DEBUG_PRINTLN(WiFi.softAPSSID());

  // print your board's IP address:
  IPAddress ip = WiFi.softAPIP();
  DEBUG_PRINT("IP Address: ");
  DEBUG_PRINTLN(ip);
}