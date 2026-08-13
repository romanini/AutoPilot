#include <Arduino.h>

// Function to calculate the checksum for NMEA sentences
String calculateChecksum(String sentence) {
  byte checksum = 0;
  for (int i = 1; i < sentence.length(); i++) {
    checksum ^= sentence[i];
  }
  return String(checksum, HEX).toUpperCase();
}

// Function to generate a GPRMC sentence
String generateGPRMC(float lat, char ns, float lon, char ew, float speed, float course) {
  String sentence = "GPRMC,120000.000,A,";
  sentence += String(lat, 4) + "," + ns + "," + String(lon, 4) + "," + ew + ",";
  sentence += String(speed, 1) + "," + String(course, 1) + ",210721,,,A";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Function to generate a GPGGA sentence
String generateGPGGA(float lat, char ns, float lon, char ew) {
  String sentence = "GPGGA,120000.000,";
  sentence += String(lat, 4) + "," + ns + "," + String(lon, 4) + "," + ew + ",1,08,1.0,10.0,M,,,,";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Function to generate a GPGLL sentence
String generateGPGLL(float lat, char ns, float lon, char ew) {
  String sentence = "GPGLL,";
  sentence += String(lat, 4) + "," + ns + "," + String(lon, 4) + "," + ew + ",120000.000,A,A";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Function to generate a GPBWC sentence
String generateGPBWC(float lat, char ns, float lon, char ew, float bearing, float distance) {
  String sentence = "GPBWC,120000.000,";
  sentence += String(lat, 4) + "," + ns + "," + String(lon, 4) + "," + ew + "," + String(bearing, 1) + ",T,,M," + String(distance, 1) + ",N,DEST";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Function to generate a GPVTG sentence
String generateGPVTG(float speed, float course) {
  String sentence = "GPVTG," + String(course, 1) + ",T,,M," + String(speed, 1) + ",N,";
  sentence += String(speed * 1.852, 1) + ",K,A";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Function to generate a GPXTE sentence
String generateGPXTE(float xte, char dir) {
  String sentence = "GPXTE,A,A," + String(xte, 2) + "," + dir + ",N";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Function to generate a GPRMB sentence
String generateGPRMB(float xte, char dir, float lat, char ns, float lon, char ew, float distance, float bearing, float speed) {
  String sentence = "GPRMB,A," + String(xte, 2) + "," + dir + ",START,DEST,";
  sentence += String(lat, 4) + "," + ns + "," + String(lon, 4) + "," + ew + ",";
  sentence += String(distance, 1) + "," + String(bearing, 1) + "," + String(speed, 1) + ",A";
  sentence = "$" + sentence + "*" + calculateChecksum(sentence);
  return sentence;
}

// Variables to simulate GPS data
float latitude = 4916.45;
float longitude = -12311.12;
float speed = 5.0;
float course = 90.0;
float xte = 0.0;
char xteDir = 'R';

// Variables to simulate straying
float strayingMagnitude = 0.02;
float strayingFrequency = 0.5;
unsigned long lastUpdateTimeXTE = 0;
unsigned long lastUpdateTimeRMB = 0;
unsigned long updateInterval = 1000; // 1 Hz

// Timers for other NMEA sentences
unsigned long lastUpdateTimeGPRMC = 0;
unsigned long lastUpdateTimeGPGGA = 0;
unsigned long lastUpdateTimeGPGLL = 0;
unsigned long lastUpdateTimeGPBWC = 0;
unsigned long lastUpdateTimeGPVTG = 0;

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);
}

// Function to send NMEA sentence to both Serial and Serial1
void sendNMEASentence(String sentence) {
  Serial.println(sentence);
  Serial1.println(sentence);
}

void loop() {
  unsigned long currentTime = millis();

  // Simulate straying by adding a sine wave component to the cross-track error
  xte = strayingMagnitude * sin(2 * PI * strayingFrequency * (currentTime / 1000.0));
  xteDir = (xte >= 0) ? 'R' : 'L';
  xte = abs(xte);

  // Send XTE and RMB sentences every second
  if (currentTime - lastUpdateTimeXTE >= updateInterval) {
    sendNMEASentence(generateGPXTE(xte, xteDir));
    lastUpdateTimeXTE = currentTime;
  }
  if (currentTime - lastUpdateTimeRMB >= updateInterval) {
    sendNMEASentence(generateGPRMB(xte, xteDir, latitude, 'N', longitude, 'W', 10.5, course, speed));
    lastUpdateTimeRMB = currentTime;
  }

  // Randomly send other NMEA sentences
  if (currentTime - lastUpdateTimeGPRMC >= random(900, 1100)) {
    sendNMEASentence(generateGPRMC(latitude, 'N', longitude, 'W', speed, course));
    lastUpdateTimeGPRMC = currentTime;
  }
  if (currentTime - lastUpdateTimeGPGGA >= random(900, 1100)) {
    sendNMEASentence(generateGPGGA(latitude, 'N', longitude, 'W'));
    lastUpdateTimeGPGGA = currentTime;
  }
  if (currentTime - lastUpdateTimeGPGLL >= random(900, 1100)) {
    sendNMEASentence(generateGPGLL(latitude, 'N', longitude, 'W'));
    lastUpdateTimeGPGLL = currentTime;
  }
  if (currentTime - lastUpdateTimeGPBWC >= random(900, 1100)) {
    sendNMEASentence(generateGPBWC(latitude, 'N', longitude, 'W', course, 10.5));
    lastUpdateTimeGPBWC = currentTime;
  }
  if (currentTime - lastUpdateTimeGPVTG >= random(900, 1100)) {
    sendNMEASentence(generateGPVTG(speed, course));
    lastUpdateTimeGPVTG = currentTime;
  }
}

