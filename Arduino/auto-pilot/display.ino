#include <SPI.h>
#include "Adafruit_GFX.h"
#include "Adafruit_HX8357.h"
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <TimeLib.h>

// These are 'flexible' lines that can be changed
#define TFT_CS 10
#define TFT_DC 9
#define TFT_RST 8  // RST can be set to -1 if you tie it to Arduino's reset
#define DISPLAY_UPDATE_RATE 400
#define DISPLAY_SEGMENTS 15

int display_refresh_rate[DISPLAY_SEGMENTS]; 

uint32_t display_refresh_timer[DISPLAY_SEGMENTS];
int display_refresh_selector = 0;

// Use hardware SPI (on Uno, #13, #12, #11) and the above for CS/DC
Adafruit_HX8357 tft = Adafruit_HX8357(TFT_CS, TFT_DC, TFT_RST);

void setup_display() {
  tft.begin();

  // read diagnostics (optional but can help debug problems)
  uint8_t x = tft.readcommand8(HX8357_RDPOWMODE);
  Serial.print("Display Power Mode: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDMADCTL);
  Serial.print("MADCTL Mode: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDCOLMOD);
  Serial.print("Pixel Format: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDDIM);
  Serial.print("Image Format: 0x");
  Serial.println(x, HEX);
  x = tft.readcommand8(HX8357_RDDSDR);
  Serial.print("Self Diagnostic: 0x");
  Serial.println(x, HEX);

  tft.setRotation(1);
  tft.fillScreen(HX8357_BLACK);
  initialize_display();
  initialize_refresh_rates();

  for (int i = 0; i < DISPLAY_SEGMENTS; i++) {
    display_refresh_timer[i] = millis();
  }
}

// You would think we can do this in a static initializtion but for some odd reasosn that causes very strange warnings about being offset by 4 bytes
// so fine.. I'll initialze it this way which solves the warning.
void initialize_refresh_rates() {
  for (int i = 0; i < DISPLAY_SEGMENTS; i++) {
    switch (i) {
      case 0: // speed
        display_refresh_rate[i] = 750;
        break;
      case 1: // heading long average
        display_refresh_rate[i] = 700;
        break;
      case 2: // heading long average change
        display_refresh_rate[i] = 700;
        break;
      case 3: // heading short average
        display_refresh_rate[i] = 600;
        break;
      case 4: // heading short average change
        display_refresh_rate[i] = 600;
        break;
      case 5:  // heading
        display_refresh_rate[i] = 400;
        break;
      case 6: // bearing
        display_refresh_rate[i] = 900;
        break;
      case 7: // bearing correction
        display_refresh_rate[i] = 900;
        break;
      case 8: // motor
        display_refresh_rate[i] = 250;
        break;
      case 9: // distance
        display_refresh_rate[i] = 1250;
        break;
      case 10: //course
        display_refresh_rate[i] = 800;
        break;
      case 11: // location lat
        display_refresh_rate[i] = 850;
        break;
      case 12: // location lon
        display_refresh_rate[i] = 850;
        break;
      case 13: // date & time
        display_refresh_rate[i] = 10000;
        break;
      case 14: // fix
        display_refresh_rate[i] = 15000;
        break;
    }
  }
}

// This segmentation of the display function is done because refreshing the whole display takes 500ms to 800ms so if we refrehs the
// whole display in the loop() function (even if once it a while) it will prevent the sensors and buttons from doing anything for that long
// this makes the whole thing unresponsive.  Since we are single threaded and can't refresh the display in its own thread what we do is
// effectively time-slice the display() function by having it refresh a small part of the display each time.  For the distination we special case
// that one since it changes so infrequently that we only refresh it when it changes.
void display(AutoPilot& autoPilot) {
  if (autoPilot.hasModeChanged()) {
    display_mode(autoPilot);
  } else if (autoPilot.hasDestinationChanged()) {
    // as this takes more than 300ms to display we can just do it when something changes.
    display_destination(autoPilot);
  } else if (millis() - display_refresh_timer[display_refresh_selector] > display_refresh_rate[display_refresh_selector]) {
    display_refresh_timer[display_refresh_selector] = millis();
    switch (display_refresh_selector) {
      case 0:
        display_speed(autoPilot);
        break;
      case 1:
        display_heading_long_average(autoPilot);
        break;
      case 2:
        display_heading_long_average_change(autoPilot);
        break;
      case 3:
        display_heading_short_average(autoPilot);
        break;
      case 4:
        display_heading_short_average_change(autoPilot);
        break;
      case 5:
        display_heading(autoPilot);
        break;
      case 6:
        display_bearing(autoPilot);
        break;
      case 7:
        display_bearing_correction(autoPilot);
        break;
      case 8:
        display_motor(autoPilot);
        break;
      case 9:
        display_distance(autoPilot);
        break;
      case 10:
        display_course(autoPilot);
        break;
      case 11:
        display_location_lat(autoPilot);
        break;
      case 12:
        display_location_lon(autoPilot);
        break;
      case 13:
        display_datetime(autoPilot);
        break;
      case 14:
        display_fix(autoPilot);
        break;
    }
  }
  display_refresh_selector = ((display_refresh_selector + 1) % DISPLAY_SEGMENTS);
}

void display_speed(AutoPilot& autoPilot) {
  static GFXcanvas1 speed_value_canvas(90, 42);
  speed_value_canvas.fillScreen(HX8357_BLACK);
  speed_value_canvas.setTextColor(HX8357_WHITE);
  speed_value_canvas.setFont(&FreeSansBold24pt7b);
  speed_value_canvas.setCursor(0, 37);
  speed_value_canvas.print(autoPilot.getSpeed(), 2);
  tft.drawBitmap(20, 23, speed_value_canvas.getBuffer(), 90, 42, HX8357_CYAN, HX8357_BLACK);
}

void display_heading_long_average(AutoPilot& autoPilot) {
  static GFXcanvas1 compass_la_value_canvas(107, 32);
  compass_la_value_canvas.fillScreen(HX8357_BLACK);
  compass_la_value_canvas.setTextColor(HX8357_WHITE);
  compass_la_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_la_value_canvas.setCursor(0, 29);
  compass_la_value_canvas.print(autoPilot.getHeadingLongAverage());
  tft.drawBitmap(20, 105, compass_la_value_canvas.getBuffer(), 107, 32, HX8357_YELLOW, HX8357_BLACK);
}

void display_heading_long_average_change(AutoPilot& autoPilot) {
  static GFXcanvas1 compass_la_change_value_canvas(95, 32);
  compass_la_change_value_canvas.fillScreen(HX8357_BLACK);
  compass_la_change_value_canvas.setTextColor(HX8357_WHITE);
  compass_la_change_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_la_change_value_canvas.setCursor(0, 29);
  compass_la_change_value_canvas.print("~");
  compass_la_change_value_canvas.print(autoPilot.getHeadingLongAverageChange());
  tft.drawBitmap(35, 143, compass_la_change_value_canvas.getBuffer(), 95, 32, HX8357_YELLOW, HX8357_BLACK);
}

void display_heading_short_average(AutoPilot& autoPilot) {
  static GFXcanvas1 compass_sa_value_canvas(107, 32);
  compass_sa_value_canvas.fillScreen(HX8357_BLACK);
  compass_sa_value_canvas.setTextColor(HX8357_WHITE);
  compass_sa_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_sa_value_canvas.setCursor(0, 29);
  compass_sa_value_canvas.print(autoPilot.getHeadingShortAverage());
  tft.drawBitmap(20, 190, compass_sa_value_canvas.getBuffer(), 107, 32, HX8357_YELLOW, HX8357_BLACK);
}

void display_heading_short_average_change(AutoPilot& autoPilot) {
  static GFXcanvas1 compass_sa_change_value_canvas(95, 32);
  compass_sa_change_value_canvas.fillScreen(HX8357_BLACK);
  compass_sa_change_value_canvas.setTextColor(HX8357_WHITE);
  compass_sa_change_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_sa_change_value_canvas.setCursor(0, 29);
  compass_sa_change_value_canvas.print("~");
  compass_sa_change_value_canvas.print(autoPilot.getHeadingShortAverageChange());
  tft.drawBitmap(35, 227, compass_sa_change_value_canvas.getBuffer(), 95, 32, HX8357_YELLOW, HX8357_BLACK);
}

void display_heading(AutoPilot& autoPilot) {
  static GFXcanvas1 compass_sa_value_canvas(107, 32);
  compass_sa_value_canvas.fillScreen(HX8357_BLACK);
  compass_sa_value_canvas.setTextColor(HX8357_WHITE);
  compass_sa_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_sa_value_canvas.setCursor(0, 29);
  compass_sa_value_canvas.print(autoPilot.getHeading());
  tft.drawBitmap(20, 276, compass_sa_value_canvas.getBuffer(), 107, 32, HX8357_YELLOW, HX8357_BLACK);
}

void display_mode(AutoPilot& autoPilot) {
  static GFXcanvas1 mode_value_canvas(115, 24);
  mode_value_canvas.fillScreen(HX8357_BLACK);
  if (autoPilot.getMode() == 2) {
    mode_value_canvas.setTextColor(HX8357_WHITE);
    mode_value_canvas.setFont(&FreeSansBold12pt7b);
    mode_value_canvas.setCursor(5, 18);
    mode_value_canvas.print("navigate");
  } else if (autoPilot.getMode() == 1) {
    mode_value_canvas.setTextColor(HX8357_WHITE);
    mode_value_canvas.setFont(&FreeSansBold12pt7b);
    mode_value_canvas.setCursor(0, 18);
    mode_value_canvas.print("compass");
  }
  tft.drawBitmap(185, 20, mode_value_canvas.getBuffer(), 115, 24, 0xF57F, HX8357_BLACK);
}

void display_destination(AutoPilot& autoPilot) {
  static GFXcanvas1 destination_value_canvas(139, 50);
  destination_value_canvas.fillScreen(HX8357_BLACK);
  if (autoPilot.getMode() == 2) {
    destination_value_canvas.setTextColor(HX8357_WHITE);
    destination_value_canvas.setFont(&FreeSansBold12pt7b);
    destination_value_canvas.setCursor(23, 18);
    destination_value_canvas.print(autoPilot.getWaypointLat(), 6);
    destination_value_canvas.setCursor(0, 45);
    destination_value_canvas.println(autoPilot.getWaypointLon(), 6);
  } else if (autoPilot.getMode() == 1) {
    destination_value_canvas.setTextColor(HX8357_WHITE);
    destination_value_canvas.setFont(&FreeSansBold24pt7b);
    destination_value_canvas.setCursor(10, 35);
    destination_value_canvas.print(autoPilot.getHeadingDesired(), 1);
  } else {
    destination_value_canvas.setTextColor(HX8357_WHITE);
    destination_value_canvas.setFont(&FreeSansBold24pt7b);
    destination_value_canvas.setCursor(30, 35);
    destination_value_canvas.print("N/A");
  }
  tft.drawBitmap(171, 52, destination_value_canvas.getBuffer(), 139, 50, 0xF57F, HX8357_BLACK);
}

void display_bearing(AutoPilot& autoPilot) {
  if (autoPilot.getMode() > 0) {
    static GFXcanvas1 bearing_value_canvas(115, 42);
    bearing_value_canvas.fillScreen(HX8357_BLACK);
    bearing_value_canvas.setFont(&FreeSansBold24pt7b);
    bearing_value_canvas.setTextColor(HX8357_WHITE);
    bearing_value_canvas.setCursor(0, 37);
    bearing_value_canvas.print(autoPilot.getBearing(), 1);
    tft.drawBitmap(182, 138, bearing_value_canvas.getBuffer(), 115, 42, 0xFC09, HX8357_BLACK);
  }
}

void display_bearing_correction(AutoPilot& autoPilot) {
  if (autoPilot.getMode() > 0) {
    static GFXcanvas1 bearing_correction_value_canvas(107, 32);
    bearing_correction_value_canvas.fillScreen(HX8357_BLACK);
    bearing_correction_value_canvas.setTextColor(HX8357_WHITE);
    bearing_correction_value_canvas.setFont(&FreeSansBold18pt7b);
    bearing_correction_value_canvas.setCursor(0, 29);
    bearing_correction_value_canvas.print((autoPilot.getBearingCorrection() > 0) ? autoPilot.getBearingCorrection() : autoPilot.getBearingCorrection() * -1.0, 1);
    bearing_correction_value_canvas.println((autoPilot.getBearingCorrection() > 0) ? " R" : " L");
    tft.drawBitmap(187, 193, bearing_correction_value_canvas.getBuffer(), 107, 32, 0xFC09, HX8357_BLACK);
  }
}

void display_motor(AutoPilot& autoPilot) {
  if (autoPilot.getMode() > 0) {
    static GFXcanvas1 bearing_correction_value_canvas(105, 18);
    bearing_correction_value_canvas.fillScreen(HX8357_BLACK);
    bearing_correction_value_canvas.setTextColor(HX8357_WHITE);
    bearing_correction_value_canvas.setFont(&FreeSansBold12pt7b);
    int motor_time_left = autoPilot.getMotorStopTime() - millis();
    if (autoPilot.getMotorDirection() < 0) {
      if (motor_time_left > 1000) {
        bearing_correction_value_canvas.setCursor(0, 14);
        bearing_correction_value_canvas.print("<-----");
      } else if (motor_time_left > 750) {
        bearing_correction_value_canvas.setCursor(10, 14);
        bearing_correction_value_canvas.print("<----");
      } else if (motor_time_left > 500) {
        bearing_correction_value_canvas.setCursor(20, 14);
        bearing_correction_value_canvas.print("<---");
      } else if (motor_time_left > 250) {
        bearing_correction_value_canvas.setCursor(30, 14);
        bearing_correction_value_canvas.print("<--");
      } else if (motor_time_left > 0) {
        bearing_correction_value_canvas.setCursor(40, 14);
        bearing_correction_value_canvas.print("<-");
      }
    } else if (autoPilot.getMotorDirection() > 0) {
      if (motor_time_left > 1000) {
        bearing_correction_value_canvas.setCursor(50, 14);
        bearing_correction_value_canvas.print("----->");
      } else if (motor_time_left > 750) {
        bearing_correction_value_canvas.setCursor(50, 14);
        bearing_correction_value_canvas.print("---->");
      } else if (motor_time_left > 500) {
        bearing_correction_value_canvas.setCursor(50, 14);
        bearing_correction_value_canvas.print("--->");
      } else if (motor_time_left > 250) {
        bearing_correction_value_canvas.setCursor(50, 14);
        bearing_correction_value_canvas.print("-->");
      } else if (motor_time_left > 0) {
        bearing_correction_value_canvas.setCursor(50, 14);
        bearing_correction_value_canvas.print("->");
      }
    } else {
      bearing_correction_value_canvas.setCursor(50, 14);
      bearing_correction_value_canvas.print("-");
    }
    tft.drawBitmap(188, 230, bearing_correction_value_canvas.getBuffer(), 105, 18, 0xFC09, HX8357_BLACK);
  }
}

void display_distance(AutoPilot& autoPilot) {
  if (autoPilot.isWaypointSet()) {
    static GFXcanvas1 distance_value_canvas(90, 42);
    distance_value_canvas.fillScreen(HX8357_BLACK);
    distance_value_canvas.setTextColor(HX8357_WHITE);
    distance_value_canvas.setFont(&FreeSansBold24pt7b);
    distance_value_canvas.setCursor(0, 37);
    distance_value_canvas.print(autoPilot.getDistance(), 2);
    tft.drawBitmap(341, 23, distance_value_canvas.getBuffer(), 90, 42, HX8357_CYAN, HX8357_BLACK);
  }
}

void display_course(AutoPilot& autoPilot) {
  if (autoPilot.hasFix()) {
    static GFXcanvas1 course_value_canvas(115, 42);
    course_value_canvas.fillScreen(HX8357_BLACK);
    course_value_canvas.setTextColor(HX8357_WHITE);
    course_value_canvas.setFont(&FreeSansBold24pt7b);
    course_value_canvas.setCursor(0, 40);
    course_value_canvas.print(autoPilot.getCourse(), 1);
    tft.drawBitmap(341, 113, course_value_canvas.getBuffer(), 115, 42, 0x7FE8, HX8357_BLACK);
  }
}

void display_location_lat(AutoPilot& autoPilot) {
  if (autoPilot.hasFix()) {
    static GFXcanvas1 location_lat_value_canvas(115, 22);
    location_lat_value_canvas.fillScreen(HX8357_BLACK);
    location_lat_value_canvas.setTextColor(HX8357_WHITE);
    location_lat_value_canvas.setFont(&FreeSansBold12pt7b);
    location_lat_value_canvas.setCursor(0, 18);
    location_lat_value_canvas.print(autoPilot.getLocationLat(), 6);
    tft.drawBitmap(354, 200, location_lat_value_canvas.getBuffer(), 115, 22, 0x7FE8, HX8357_BLACK);
  }
}

void display_location_lon(AutoPilot& autoPilot) {
  if (autoPilot.hasFix()) {
    static GFXcanvas1 location_lon_value_canvas(139, 22);
    location_lon_value_canvas.fillScreen(HX8357_BLACK);
    location_lon_value_canvas.setTextColor(HX8357_WHITE);
    location_lon_value_canvas.setFont(&FreeSansBold12pt7b);
    location_lon_value_canvas.setCursor(0, 18);
    location_lon_value_canvas.print(autoPilot.getLocationLon(), 6);
    tft.drawBitmap(331, 230, location_lon_value_canvas.getBuffer(), 139, 22, 0x7FE8, HX8357_BLACK);
  }
}

void display_datetime(AutoPilot& autoPilot) {
  static GFXcanvas1 date_time_value_canvas(165, 22);
  date_time_value_canvas.fillScreen(HX8357_BLACK);
  date_time_value_canvas.setTextColor(HX8357_WHITE);
  date_time_value_canvas.setFont(&FreeSansBold12pt7b);
  date_time_value_canvas.setCursor(0, 18);
  char dateTimeString[13];
  time_t currentTime = autoPilot.getDateTime();
  sprintf(dateTimeString, "%d/%d/%02d %d:%02d", month(currentTime), day(currentTime), year(currentTime) % 100, hour(currentTime), minute(currentTime));
  date_time_value_canvas.print(dateTimeString);
  tft.drawBitmap(181, 293, date_time_value_canvas.getBuffer(), 165, 22, HX8357_WHITE, HX8357_BLACK);
}

void display_fix(AutoPilot& autoPilot) {
  static GFXcanvas1 gps_fix_value_canvas(110, 22);
  gps_fix_value_canvas.fillScreen(HX8357_BLACK);
  gps_fix_value_canvas.setTextColor(HX8357_WHITE);
  gps_fix_value_canvas.setFont(&FreeSansBold12pt7b);
  gps_fix_value_canvas.setCursor(0, 18);
  if (autoPilot.hasFix()) {
    if (autoPilot.getFixquality() == 0) {
      gps_fix_value_canvas.print("n/a");
    } else if (autoPilot.getFixquality() == 1) {
      gps_fix_value_canvas.print("GPS");
    } else if (autoPilot.getFixquality() == 2) {
      gps_fix_value_canvas.print("DGPS");
    } else {
      // if it is not 1 or 2 let's display it so we can figure out what it is.
      gps_fix_value_canvas.print(autoPilot.getFixquality());
    }
    gps_fix_value_canvas.print("(");
    gps_fix_value_canvas.print(autoPilot.getSatellites());
    gps_fix_value_canvas.print(")");
  }

  tft.drawBitmap(360, 293, gps_fix_value_canvas.getBuffer(), 110, 22, HX8357_WHITE, HX8357_BLACK);
}

void initialize_display() {
  int16_t x1, y1;
  uint16_t w, h;

  // Canvas for Speed
  GFXcanvas1 speed_canvas(160, 80);
  speed_canvas.fillScreen(HX8357_BLACK);
  speed_canvas.setFont(&FreeSans9pt7b);
  speed_canvas.drawRect(0, 0, 160, 80, HX8357_WHITE);
  speed_canvas.getTextBounds("Speed", 0, 12, &x1, &y1, &w, &h);
  speed_canvas.fillRect(x1, y1, w + 8, h + 2, HX8357_WHITE);
  speed_canvas.setCursor(0, 14);
  speed_canvas.setTextColor(HX8357_BLACK);
  speed_canvas.print("Speed");

  tft.drawBitmap(0, 0, speed_canvas.getBuffer(), 160, 80, HX8357_CYAN, HX8357_BLACK);

  // Heading instant, short and long average
  GFXcanvas1 compass_canvas(160, 239);
  compass_canvas.fillScreen(HX8357_BLACK);
  compass_canvas.setFont(&FreeSans9pt7b);
  compass_canvas.drawRect(0, 0, 160, 239, HX8357_YELLOW);
  compass_canvas.getTextBounds("Heading", 0, 12, &x1, &y1, &w, &h);
  compass_canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  compass_canvas.setCursor(0, 12);
  compass_canvas.setTextColor(HX8357_BLACK);
  compass_canvas.print("Heading");
  compass_canvas.drawLine(0, 100, 160, 100, HX8357_YELLOW);
  compass_canvas.drawLine(0, 185, 160, 185, HX8357_YELLOW);
  tft.drawBitmap(0, 81, compass_canvas.getBuffer(), 160, 239, HX8357_YELLOW, HX8357_BLACK);

  // Canvas for GPS Coordinates lat and long
  // for current and desired location
  GFXcanvas1 destination_canvas(159, 135);
  destination_canvas.fillScreen(HX8357_BLACK);
  destination_canvas.setFont(&FreeSans9pt7b);
  destination_canvas.drawRect(0, 0, 159, 105, HX8357_YELLOW);
  destination_canvas.getTextBounds("Destination", 0, 12, &x1, &y1, &w, &h);
  destination_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_YELLOW);
  destination_canvas.setCursor(0, 14);
  destination_canvas.setTextColor(HX8357_BLACK);
  destination_canvas.print("Destination");
  tft.drawBitmap(161, 0, destination_canvas.getBuffer(), 159, 105, 0xF57F, HX8357_BLACK);

  // // Canvas for Course Correction
  GFXcanvas1 bearing_canvas(159, 163);
  bearing_canvas.fillScreen(HX8357_BLACK);
  bearing_canvas.drawRect(0, 0, 159, 163, HX8357_WHITE);
  bearing_canvas.setFont(&FreeSans9pt7b);
  bearing_canvas.getTextBounds("Bearing", 0, 12, &x1, &y1, &w, &h);
  bearing_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  bearing_canvas.setCursor(0, 14);
  bearing_canvas.setTextColor(HX8357_BLACK);
  bearing_canvas.print("Bearing");
  tft.drawBitmap(161, 106, bearing_canvas.getBuffer(), 159, 163, 0xFC09, HX8357_BLACK);

  // Canvas for Distance
  GFXcanvas1 distance_canvas(159, 80);
  distance_canvas.fillScreen(HX8357_BLACK);
  distance_canvas.setFont(&FreeSans9pt7b);
  distance_canvas.drawRect(0, 0, 159, 80, HX8357_WHITE);
  distance_canvas.getTextBounds("Distance", 0, 12, &x1, &y1, &w, &h);
  distance_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  distance_canvas.setCursor(0, 14);
  distance_canvas.setTextColor(HX8357_BLACK);
  distance_canvas.print("Distance");
  tft.drawBitmap(321, 0, distance_canvas.getBuffer(), 159, 80, HX8357_CYAN, HX8357_BLACK);

  // Canvas for Course and Bearing
  GFXcanvas1 gps_canvas(159, 190);
  gps_canvas.fillScreen(HX8357_BLACK);
  gps_canvas.drawRect(0, 0, 159, 94, HX8357_WHITE);
  gps_canvas.setFont(&FreeSans9pt7b);
  gps_canvas.getTextBounds("Course", 0, 12, &x1, &y1, &w, &h);
  gps_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  gps_canvas.setCursor(0, 14);
  gps_canvas.setTextColor(HX8357_BLACK);
  gps_canvas.print("Course");
  gps_canvas.drawRect(0, 94, 159, 94, HX8357_YELLOW);
  gps_canvas.setFont(&FreeSans9pt7b);
  gps_canvas.getTextBounds("Location", 0, 106, &x1, &y1, &w, &h);
  gps_canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  gps_canvas.setCursor(0, 106);
  gps_canvas.setTextColor(HX8357_BLACK);
  gps_canvas.print("Location");
  tft.drawBitmap(321, 81, gps_canvas.getBuffer(), 159, 190, 0x7FE8, HX8357_BLACK);

  // Canvas for Date and Time
  GFXcanvas1 date_time_canvas(319, 50);
  date_time_canvas.fillScreen(HX8357_BLACK);
  date_time_canvas.setFont(&FreeSans9pt7b);
  date_time_canvas.drawRect(0, 0, 319, 50, HX8357_WHITE);
  date_time_canvas.getTextBounds("Date &Time", 0, 12, &x1, &y1, &w, &h);
  date_time_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  date_time_canvas.setCursor(0, 14);
  date_time_canvas.setTextColor(HX8357_BLACK);
  date_time_canvas.print("Date & Time");
  tft.drawBitmap(161, 270, date_time_canvas.getBuffer(), 319, 50, HX8357_WHITE, HX8357_BLACK);
}