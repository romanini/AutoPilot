
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
  Serial.print("Using ");
  Serial.println(WIFI_LIB_NAME);


//  WiFi.config(ip, dns, gateway, subnet);
    WiFi.softAPConfig(ip, gateway, subnet);
//  WiFi.mode(WIFI_STA);

  Serial.print("Attempting to connect to SSID: ");
  Serial.println(ssid);
  // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
  wifi_status = WiFi.begin(ssid, pass);
//  wifi_status = WiFi.softAP(ssid, pass);
  // attempt to connect to WiFi network:
  // while (WiFi.status() != WL_CONNECTED) {
  while (!wifi_status) {
    delay(500);
  }

  print_wifi_status();
}

void print_wifi_status() {

  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.softAPSSID());

  // print your board's IP address:
  IPAddress ip = WiFi.softAPIP();
  Serial.print("IP Address: ");
  Serial.println(ip);
}