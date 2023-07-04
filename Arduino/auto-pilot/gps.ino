#include <Adafruit_GPS.h>
#include <TimeLib.h>
#include <Timezone.h>

// what's the name of the hardware serial port?
#define GPSSerial Serial1
#define GPS_UPDATE_RATE 500

// Connect to the GPS on the hardware port
Adafruit_GPS gps = Adafruit_GPS(&GPSSerial);

TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};  //UTC - 4 hours
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};   //UTC - 5 hours
Timezone pacificTz(usPDT, usPST);

uint32_t gps_timer = millis();

void setup_gps() {
  // 9600 NMEA is the default baud rate for Adafruit MTK GPS's- some use 4800
  gps.begin(9600);
  // uncomment this line to turn on RMC (recommended minimum) and GGA (fix data) including altitude
  gps.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA);
  // uncomment this line to turn on only the "minimum recommended" data
  //gps.sendCommand(PMTK_SET_NMEA_OUTPUT_ALLDATA);
  // For parsing data, we don't suggest using anything but either RMC only or RMC+GGA since
  // the parser doesn't care about other sentences at this time
  // Set the update rate
  gps.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);  // 1 Hz update rate

  delay(1000);

  // Ask for firmware version
  Serial.println("GPS Firmware");
  gps.println(PMTK_Q_RELEASE);
  Serial.println("GPS all setup");
}

void check_gps(AutoPilot& autoPilot) {
  // read data from the GPS in the 'main loop'
  char c = gps.read();

  // if a sentence is received, we can check the checksum, parse it...
  if (gps.newNMEAreceived()) {
    if (!gps.parse(gps.lastNMEA())) {  // this also sets the newNMEAreceived() flag to false
      return;                          // we can fail to parse a sentence in which case we should just wait for another
    }
  }

  // approximately every 1 seconds or so, print out the current stats
  if (millis() - gps_timer > GPS_UPDATE_RATE) {
    gps_timer = millis();  // reset the timer
    autoPilot.setFix(gps.fix);
    if (gps.fix) {
      tmElements_t timeComponents;
      timeComponents.Year = 2000 + gps.year - 1970;
      timeComponents.Month = gps.month;
      timeComponents.Day = gps.day;
      timeComponents.Hour = gps.hour;
      timeComponents.Minute = gps.minute;
      timeComponents.Second = 0;
      time_t currentTime = makeTime(timeComponents);
      autoPilot.setDateTime(pacificTz.toLocal(currentTime));
      autoPilot.setFixquality(gps.fixquality, gps.satellites);
      autoPilot.setSpeed(gps.speed);
      autoPilot.setLoation(gps.latitudeDegrees, gps.longitudeDegrees, gps.angle);
    }
  }
}

void print_gps() {
  Serial.println("GPS fix");
  Serial.print(gps.year);
  Serial.print("/");
  Serial.print(gps.month);
  Serial.print("/");
  Serial.print(gps.day);
  Serial.print(" ");
  Serial.print(gps.hour);
  Serial.print(":");
  Serial.println(gps.minute);
  Serial.print(gps.fix);
  Serial.print(" / ");
  Serial.print(gps.fixquality);
  Serial.print(" / ");
  Serial.print(gps.fixquality_3d);
  Serial.print(" / ");
  Serial.println(gps.satellites);
  Serial.print(gps.speed);
  Serial.print(" / ");
  Serial.print(gps.latitudeDegrees,14);
  Serial.print(" / ");
  Serial.print(gps.longitudeDegrees,14);
  Serial.print(" / ");
  Serial.println(gps.angle);
}