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
#define TFT_CS D10
#define TFT_DC D9
#define TFT_RST -1  // RST can be set to -1 if you tie it to Arduino's reset

#define SPI_MISO D12
#define SPI_MOSI D11
#define SPI_SCLK D13

static constexpr uint32_t TFT_SPI_HZ = 24000000; // try 40 MHz; drop to 24 MHz if unstable

Adafruit_HX8357 tft = Adafruit_HX8357(&SPI, TFT_CS, TFT_DC, TFT_RST);

uint16_t backgroundColor = HX8357_BLACK;

// Tracks the last value painted for each display item.
// Initialized to sentinels in initialize_displayed_values() so everything
// paints on the first call to display().
struct {
  bool bearing_nav_enabled;
  bool bearing_correction_nav_enabled;

  float speed;
  bool  speed_hasFix;

  float heading;
  bool  heading_connected;

  float pitch;
  bool  pitch_connected;

  float roll;
  bool  roll_connected;

  int  stability;
  bool stability_connected;

  float bearing;
  int   bearing_mode;

  float bearingCorrection;
  int   bearingCorrection_mode;

  float distance;
  bool  distance_waypointSet;
  bool  distance_hasFix;

  float course;
  bool  course_hasFix;

  float locationLat;
  bool  lat_hasFix;

  float locationLon;
  bool  lon_hasFix;

  int  dtMonth, dtDay, dtYear, dtHour, dtMinute;
  bool dt_hasFix;

  int  fixquality;
  int  satellites;
  bool fix_hasFix;

  float batteryVoltage;
  float inputVoltage;
} disp;

void initialize_displayed_values() {
  disp.bearing_nav_enabled = false;   disp.bearing_correction_nav_enabled = false;
  disp.speed = -999.0f;          disp.speed_hasFix = false;
  disp.heading = -999.0f;        disp.heading_connected = false;
  disp.pitch = -999.0f;          disp.pitch_connected = false;
  disp.roll = -999.0f;           disp.roll_connected = false;
  disp.stability = -1;           disp.stability_connected = false;
  disp.bearing = -999.0f;        disp.bearing_mode = -1;
  disp.bearingCorrection = -999.0f; disp.bearingCorrection_mode = -1;
  disp.distance = -999.0f;       disp.distance_waypointSet = false; disp.distance_hasFix = false;
  disp.course = -999.0f;         disp.course_hasFix = false;
  disp.locationLat = -999.0f;    disp.lat_hasFix = false;
  disp.locationLon = -999.0f;    disp.lon_hasFix = false;
  disp.dtMonth = -1; disp.dtDay = -1; disp.dtYear = -1;
  disp.dtHour = -1;  disp.dtMinute = -1; disp.dt_hasFix = false;
  disp.fixquality = -1;          disp.satellites = -1; disp.fix_hasFix = false;
  disp.batteryVoltage = -999.0f; disp.inputVoltage = -999.0f;
}

void setup_screen() {
  DEBUG_PRINTLN("Starting Setup Display");

  // Control lines defaults, then start hardware SPI (SCK, MISO, MOSI, SS)
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);

  SPI.begin(SPI_SCLK, SPI_MISO, SPI_MOSI, TFT_CS);

  DEBUG_PRINTLN("Setting up Display");
  tft.begin(TFT_SPI_HZ);
  delay(20);
  DEBUG_PRINTLN("started");

  // read diagnostics (optional but can help debug problems)
  uint8_t x = tft.readcommand8(HX8357_RDPOWMODE);
  DEBUG_PRINT("Display Power Mode: 0x");
  DEBUG_PRINTLN2(x, HEX);
  x = tft.readcommand8(HX8357_RDMADCTL);
  DEBUG_PRINT("MADCTL Mode: 0x");
  DEBUG_PRINTLN2(x, HEX);
  x = tft.readcommand8(HX8357_RDCOLMOD);
  DEBUG_PRINT("Pixel Format: 0x");
  DEBUG_PRINTLN2(x, HEX);
  x = tft.readcommand8(HX8357_RDDIM);
  DEBUG_PRINT("Image Format: 0x");
  DEBUG_PRINTLN2(x, HEX);
  x = tft.readcommand8(HX8357_RDDSDR);
  DEBUG_PRINT("Self Diagnostic: 0x");
  DEBUG_PRINTLN2(x, HEX);

  tft.setRotation(1);
  tft.fillScreen(HX8357_BLACK);
  initialize_display();
  initialize_displayed_values();
}

// On the dual-core ESP32 the display runs on its own task (Core 0) so we no
// longer need to time-slice repaints across calls.  Each display_<item>()
// function tracks the last value it painted and skips the repaint if nothing
// has changed, keeping the display as snappy and current as possible.
void display() {
  // mode/destination use one-shot consuming flags in AutoPilot, so keep them event-driven.
  if (autoPilot.hasModeChanged()) {
    DEBUG_PRINTLN("display Mode ");
    display_mode();
    DEBUG_PRINTLN("done display Mode");
  }
  if (autoPilot.hasDestinationChanged()) {
    DEBUG_PRINTLN("display destination");
    display_destination();
    DEBUG_PRINTLN("done display destination");
  }

  display_speed();
  display_heading();
  display_pitch();
  display_roll();
  display_stability();
  display_bearing();
  display_bearing_correction();
  display_distance();
  display_course();
  display_location_lat();
  display_location_lon();
  display_datetime();
  display_fix();
  display_volts();
}

void display_speed() {
  float cur_speed  = autoPilot.getSpeed();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_speed == disp.speed && cur_hasFix == disp.speed_hasFix) return;
  disp.speed      = cur_speed;
  disp.speed_hasFix = cur_hasFix;

  GFXcanvas1 speed_value_canvas(90, 42);
  uint16_t foregroundColor = HX8357_CYAN;
  speed_value_canvas.fillScreen(0);  // Background index

  if (cur_hasFix) {
    speed_value_canvas.setTextColor(1);  // Foreground index
    speed_value_canvas.setFont(&FreeSansBold24pt7b);
    speed_value_canvas.setCursor(0, 37);
    speed_value_canvas.print(cur_speed, 2);
  }
  // If no fix, the canvas remains cleared (black background)
  // and will be drawn as such, effectively showing nothing or just background.
  tft.drawBitmap(20, 23, speed_value_canvas.getBuffer(), 90, 42, foregroundColor, backgroundColor);
}

void display_heading() {
  float cur_heading   = autoPilot.getHeading();
  bool  cur_connected = autoPilot.isConnected();
  if (cur_heading == disp.heading && cur_connected == disp.heading_connected) return;
  disp.heading           = cur_heading;
  disp.heading_connected = cur_connected;

  GFXcanvas1 compass_value_canvas(107, 32);
  uint16_t foregroundColor = HX8357_YELLOW;
  compass_value_canvas.fillScreen(0);  // Background index

  compass_value_canvas.setTextColor(1);  // Foreground index
  compass_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_value_canvas.setCursor(0, 29);
  if (cur_connected) {
    compass_value_canvas.print(cur_heading);
  }
  tft.drawBitmap(20, 101, compass_value_canvas.getBuffer(), 107, 32, foregroundColor, backgroundColor);
}

void display_pitch() {
  float cur_pitch     = autoPilot.getPitch();
  bool  cur_connected = autoPilot.isConnected();
  if (cur_pitch == disp.pitch && cur_connected == disp.pitch_connected) return;
  disp.pitch           = cur_pitch;
  disp.pitch_connected = cur_connected;

  GFXcanvas1 compass_value_canvas(107, 32);
  uint16_t foregroundColor = HX8357_YELLOW;
  compass_value_canvas.fillScreen(0);  // Background index

  compass_value_canvas.setTextColor(1);  // Foreground index
  compass_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_value_canvas.setCursor(0, 29);
  if (cur_connected) {
    compass_value_canvas.print(cur_pitch);
  }
  tft.drawBitmap(20, 158, compass_value_canvas.getBuffer(), 107, 32, foregroundColor, backgroundColor);
}

void display_roll() {
  float cur_roll      = autoPilot.getRoll();
  bool  cur_connected = autoPilot.isConnected();
  if (cur_roll == disp.roll && cur_connected == disp.roll_connected) return;
  disp.roll           = cur_roll;
  disp.roll_connected = cur_connected;

  GFXcanvas1 compass_value_canvas(107, 32);
  uint16_t foregroundColor = HX8357_YELLOW;
  compass_value_canvas.fillScreen(0);  // Background index

  compass_value_canvas.setTextColor(1);  // Foreground index
  compass_value_canvas.setFont(&FreeSansBold18pt7b);
  compass_value_canvas.setCursor(0, 29);
  if (cur_connected) {
    compass_value_canvas.print(cur_roll);
  }
  tft.drawBitmap(20, 217, compass_value_canvas.getBuffer(), 107, 32, foregroundColor, backgroundColor);
}

void display_stability() {
  int  cur_stability  = autoPilot.getStabilityClassification();
  bool cur_connected  = autoPilot.isConnected();
  if (cur_stability == disp.stability && cur_connected == disp.stability_connected) return;
  disp.stability           = cur_stability;
  disp.stability_connected = cur_connected;

  GFXcanvas1 compass_value_canvas(112, 32);
  uint16_t foregroundColor = HX8357_YELLOW;
  compass_value_canvas.fillScreen(0);  // Background index

  compass_value_canvas.setTextColor(1);  // Foreground index
  compass_value_canvas.setFont(&FreeSansBold12pt7b);
  compass_value_canvas.setCursor(0, 29);
  if (cur_connected) {
    switch (cur_stability) {
      case STABILITY_CLASSIFIER_UNKNOWN:
        compass_value_canvas.print("Unknown");
        break;
      case STABILITY_CLASSIFIER_ON_TABLE:
        compass_value_canvas.print("On Table");
        break;
      case STABILITY_CLASSIFIER_STATIONARY:
        compass_value_canvas.print("Stationary");
        break;
      case STABILITY_CLASSIFIER_STABLE:
        compass_value_canvas.print("Stable");
        break;
      case STABILITY_CLASSIFIER_MOTION:
        compass_value_canvas.print("In Motion");
        break;
    }
  }
  tft.drawBitmap(20, 279, compass_value_canvas.getBuffer(), 112, 32, foregroundColor, backgroundColor);
}

void display_mode() {
  GFXcanvas1 mode_value_canvas(115, 24);
  uint16_t foregroundColor = 0xF57F;
  mode_value_canvas.fillScreen(0);  // Background index

  mode_value_canvas.setTextColor(1);  // Foreground index
  mode_value_canvas.setFont(&FreeSansBold12pt7b);

  if (!autoPilot.isConnected()) {
      mode_value_canvas.setCursor(0, 18);
      mode_value_canvas.print("no link");
  } else {
    if (autoPilot.isNavigationEnabled()) {
      if (autoPilot.isTackRequested()) {
        mode_value_canvas.setCursor(0, 18);
        mode_value_canvas.print("<- tack ->");
      } else {
        if (autoPilot.getMode() == 2) {
          mode_value_canvas.setCursor(5, 18);
          mode_value_canvas.print("waypoint");
        } else if (autoPilot.getMode() == 1) {
          mode_value_canvas.setCursor(0, 18);
          mode_value_canvas.print("compass");
        }
      }
    } else {
      mode_value_canvas.setCursor(0, 18);
      mode_value_canvas.print("disabled");
    }
  }
  tft.drawBitmap(185, 20, mode_value_canvas.getBuffer(), 115, 24, foregroundColor, backgroundColor);
}

void display_destination() {
  GFXcanvas1 destination_value_canvas(139, 50);  // Changed to GFXcanvas1
  uint16_t foregroundColor = 0xF57F;             // Default foreground color used in this canvas

  destination_value_canvas.fillScreen(0);  // Use 0 for background index
  if (autoPilot.isNavigationEnabled()) {
    if (autoPilot.getMode() == 2) {
      destination_value_canvas.setTextColor(1);  // Use 1 for foreground index
      destination_value_canvas.setFont(&FreeSansBold12pt7b);
      destination_value_canvas.setCursor(23, 18);
      destination_value_canvas.print(autoPilot.getWaypointLat(), 6);
      destination_value_canvas.setCursor(0, 45);
      destination_value_canvas.println(autoPilot.getWaypointLon(), 6);
    } else if (autoPilot.getMode() == 1) {
      destination_value_canvas.setTextColor(1);  // Use 1 for foreground index
      destination_value_canvas.setFont(&FreeSansBold24pt7b);
      destination_value_canvas.setCursor(10, 35);
      destination_value_canvas.print(autoPilot.getHeadingDesired(), 1);
    }
  }
  tft.drawBitmap(171, 52, destination_value_canvas.getBuffer(), 139, 50, foregroundColor, backgroundColor);
}

void display_bearing() {
  bool cur_nav = autoPilot.isNavigationEnabled();
  float cur_bearing = autoPilot.getBearing();
  if (cur_bearing == disp.bearing && cur_nav == disp.bearing_nav_enabled) return;
  disp.bearing      = cur_bearing;
  disp.bearing_nav_enabled = cur_nav;

  GFXcanvas1 bearing_value_canvas(115, 42);
  uint16_t foregroundColor = 0xFC09;
  bearing_value_canvas.fillScreen(0);  // Background index
  if (autoPilot.isConnected() && autoPilot.isNavigationEnabled()) {
    bearing_value_canvas.setFont(&FreeSansBold18pt7b);
    bearing_value_canvas.setTextColor(1);  // Foreground index
    bearing_value_canvas.setCursor(0, 29);
    bearing_value_canvas.print(cur_bearing, 1);
  }
  // If mode is not > 0, canvas is cleared and drawn as such (effectively black)
  tft.drawBitmap(182, 130, bearing_value_canvas.getBuffer(), 115, 42, foregroundColor, backgroundColor);
}

void display_bearing_correction() {
  bool cur_nav = autoPilot.isNavigationEnabled();
  float cur_bc   = autoPilot.getBearingCorrection();
  if (cur_bc == disp.bearingCorrection && cur_nav == disp.bearing_correction_nav_enabled) return;
  disp.bearingCorrection      = cur_bc;
  disp.bearing_correction_nav_enabled = cur_nav;

  // Canvas is 140 wide (was 115) and drawn 10px further left so a 3-digit
  // correction like "180.0 L" has room for the L/R suffix without clipping.
  GFXcanvas1 bearing_correction_value_canvas(140, 32);
  uint16_t foregroundColor = 0xFC09;
  bearing_correction_value_canvas.fillScreen(0);  // Background index

  if (autoPilot.isConnected() && autoPilot.isNavigationEnabled()) {
    bearing_correction_value_canvas.setTextColor(1);  // Foreground index
    bearing_correction_value_canvas.setFont(&FreeSansBold18pt7b);
    bearing_correction_value_canvas.setCursor(0, 29);
    bearing_correction_value_canvas.print((cur_bc > 0) ? cur_bc : cur_bc * -1.0, 1);
    bearing_correction_value_canvas.println((cur_bc > 0) ? " R" : " L");
  }
  // If mode is not > 0, canvas is cleared and drawn as such (effectively black)
  tft.drawBitmap(177, 169, bearing_correction_value_canvas.getBuffer(), 140, 32, foregroundColor, backgroundColor);
}

void display_volts() {
  float cur_batt  = autoPilot.getBatteryVoltage();
  float cur_input = autoPilot.getInputVoltage();
  if (cur_batt == disp.batteryVoltage && cur_input == disp.inputVoltage) return;
  disp.batteryVoltage = cur_batt;
  disp.inputVoltage   = cur_input;

  GFXcanvas1 volts_value_canvas(120, 22);
  uint16_t foregroundColor = HX8357_WHITE;
  volts_value_canvas.fillScreen(0);  // Background index

  volts_value_canvas.setTextColor(1);  // Foreground index
  volts_value_canvas.setFont(&FreeSansBold12pt7b);
  volts_value_canvas.setCursor(0, 18);
  volts_value_canvas.print(cur_batt, 2);
  volts_value_canvas.print(" / ");
  volts_value_canvas.println(cur_input, 1);
  tft.drawBitmap(181, 239, volts_value_canvas.getBuffer(), 120, 22, foregroundColor, backgroundColor);
}

void display_distance() {
  float cur_distance    = autoPilot.getDistance();
  bool  cur_waypointSet = autoPilot.isWaypointSet();
  bool  cur_hasFix      = autoPilot.hasFix();
  if (cur_distance == disp.distance && cur_waypointSet == disp.distance_waypointSet && cur_hasFix == disp.distance_hasFix) return;
  disp.distance            = cur_distance;
  disp.distance_waypointSet = cur_waypointSet;
  disp.distance_hasFix     = cur_hasFix;

  GFXcanvas1 distance_value_canvas(90, 42);
  uint16_t foregroundColor = HX8357_CYAN;
  distance_value_canvas.fillScreen(0);  // Background index

  distance_value_canvas.setTextColor(1);  // Foreground index
  distance_value_canvas.setFont(&FreeSansBold24pt7b);
  distance_value_canvas.setCursor(0, 37);
  if (cur_waypointSet && cur_hasFix) {
    distance_value_canvas.print(cur_distance, 2);
  }
  tft.drawBitmap(341, 23, distance_value_canvas.getBuffer(), 90, 42, foregroundColor, backgroundColor);
}

void display_course() {
  float cur_course = autoPilot.getCourse();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_course == disp.course && cur_hasFix == disp.course_hasFix) return;
  disp.course        = cur_course;
  disp.course_hasFix = cur_hasFix;

  GFXcanvas1 course_value_canvas(115, 42);
  uint16_t foregroundColor = 0x7FE8;
  course_value_canvas.fillScreen(0);  // Background index

  if (cur_hasFix) {
    course_value_canvas.setTextColor(1);  // Foreground index
    course_value_canvas.setFont(&FreeSansBold24pt7b);
    course_value_canvas.setCursor(0, 40);
    course_value_canvas.print(cur_course, 1);
  }
  // If no fix, canvas is cleared and drawn as such
  tft.drawBitmap(341, 113, course_value_canvas.getBuffer(), 115, 42, foregroundColor, backgroundColor);
}

void display_location_lat() {
  float cur_lat    = autoPilot.getLocationLat();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_lat == disp.locationLat && cur_hasFix == disp.lat_hasFix) return;
  disp.locationLat = cur_lat;
  disp.lat_hasFix  = cur_hasFix;

  GFXcanvas1 location_lat_value_canvas(115, 22);
  uint16_t foregroundColor = 0x7FE8;
  location_lat_value_canvas.fillScreen(0);  // Background index

  if (cur_hasFix) {
    location_lat_value_canvas.setTextColor(1);  // Foreground index
    location_lat_value_canvas.setFont(&FreeSansBold12pt7b);
    location_lat_value_canvas.setCursor(0, 18);
    location_lat_value_canvas.print(cur_lat, 6);
  }
  // If no fix, canvas is cleared and drawn as such
  tft.drawBitmap(354, 200, location_lat_value_canvas.getBuffer(), 115, 22, foregroundColor, backgroundColor);
}

void display_location_lon() {
  float cur_lon    = autoPilot.getLocationLon();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_lon == disp.locationLon && cur_hasFix == disp.lon_hasFix) return;
  disp.locationLon = cur_lon;
  disp.lon_hasFix  = cur_hasFix;

  GFXcanvas1 location_lon_value_canvas(139, 22);
  uint16_t foregroundColor = 0x7FE8;
  location_lon_value_canvas.fillScreen(0);  // Background index

  if (cur_hasFix) {
    location_lon_value_canvas.setTextColor(1);  // Foreground index
    location_lon_value_canvas.setFont(&FreeSansBold12pt7b);
    location_lon_value_canvas.setCursor(0, 18);
    location_lon_value_canvas.print(cur_lon, 6);
  }
  // If no fix, canvas is cleared and drawn as such
  tft.drawBitmap(331, 230, location_lon_value_canvas.getBuffer(), 139, 22, foregroundColor, backgroundColor);
}

void display_datetime() {
  int  cur_month  = autoPilot.getMonth();
  int  cur_day    = autoPilot.getDay();
  int  cur_year   = autoPilot.getYear();
  int  cur_hour   = autoPilot.getHour();
  int  cur_minute = autoPilot.getMinute();
  bool cur_hasFix = autoPilot.hasFix();
  if (cur_month == disp.dtMonth && cur_day == disp.dtDay && cur_year == disp.dtYear &&
      cur_hour == disp.dtHour && cur_minute == disp.dtMinute && cur_hasFix == disp.dt_hasFix) return;
  disp.dtMonth  = cur_month; disp.dtDay  = cur_day;  disp.dtYear   = cur_year;
  disp.dtHour   = cur_hour;  disp.dtMinute = cur_minute; disp.dt_hasFix = cur_hasFix;

  GFXcanvas1 date_time_value_canvas(165, 22);
  uint16_t foregroundColor = HX8357_WHITE;
  date_time_value_canvas.fillScreen(0);  // Background index

  date_time_value_canvas.setTextColor(1);  // Foreground index
  date_time_value_canvas.setFont(&FreeSansBold12pt7b);
  date_time_value_canvas.setCursor(0, 18);
  if (cur_hasFix) {
    char dateTimeString[16];
    sprintf(dateTimeString, "%d/%d/%02d %d:%02d", cur_month, cur_day, cur_year % 100, cur_hour, cur_minute);
    date_time_value_canvas.print(dateTimeString);
  } else {
    date_time_value_canvas.print("");
  }
  tft.drawBitmap(181, 293, date_time_value_canvas.getBuffer(), 165, 22, foregroundColor, backgroundColor);
}

void display_fix() {
  int  cur_fixquality = autoPilot.getFixquality();
  int  cur_satellites = autoPilot.getSatellites();
  bool cur_hasFix     = autoPilot.hasFix();
  if (cur_fixquality == disp.fixquality && cur_satellites == disp.satellites && cur_hasFix == disp.fix_hasFix) return;
  disp.fixquality = cur_fixquality;
  disp.satellites = cur_satellites;
  disp.fix_hasFix = cur_hasFix;

  GFXcanvas1 gps_fix_value_canvas(110, 22);
  uint16_t foregroundColor = HX8357_WHITE;
  gps_fix_value_canvas.fillScreen(0);  // Background index

  gps_fix_value_canvas.setTextColor(1);  // Foreground index
  gps_fix_value_canvas.setFont(&FreeSansBold12pt7b);
  gps_fix_value_canvas.setCursor(0, 18);
  if (cur_hasFix) {
    if (cur_fixquality == 0) {
      gps_fix_value_canvas.print("n/a");
    } else if (cur_fixquality == 1) {
      gps_fix_value_canvas.print("GPS");
    } else if (cur_fixquality == 2) {
      gps_fix_value_canvas.print("DGPS");
    } else {
      // if it is not 1 or 2 let's display it so we can figure out what it is.
      gps_fix_value_canvas.print(cur_fixquality);
    }
    gps_fix_value_canvas.print("(");
    gps_fix_value_canvas.print(cur_satellites);
    gps_fix_value_canvas.print(")");
  }
  // If no fix, canvas is cleared and drawn as such
  tft.drawBitmap(360, 293, gps_fix_value_canvas.getBuffer(), 110, 22, foregroundColor, backgroundColor);
}

void initialize_display() {
  DEBUG_PRINTLN("Initializing display");
  initialize_speed();
  initialize_compass();
  initialize_pitch();
  initialize_roll();
  initialize_stability();
  initialize_destination();
  initialize_bearing();
  initialize_volts();
  initialize_distance();
  initialize_gps();
  initialize_date_time();
}

void initialize_speed() {
  int16_t x1, y1;
  uint16_t w, h;
  // Canvas for Speed
  GFXcanvas1 speed_canvas(160, 80);
  speed_canvas.fillScreen(HX8357_BLACK);
  speed_canvas.setFont(&FreeSans9pt7b);
  speed_canvas.drawRect(0, 0, 160, 80, HX8357_CYAN);
  speed_canvas.getTextBounds("Speed", 0, 12, &x1, &y1, &w, &h);
  speed_canvas.fillRect(x1, y1, w + 8, h + 2, HX8357_CYAN);
  speed_canvas.setCursor(0, 14);
  speed_canvas.setTextColor(HX8357_BLACK);
  speed_canvas.print("Speed");
  tft.drawBitmap(0, 0, speed_canvas.getBuffer(), 160, 80, HX8357_CYAN, HX8357_BLACK);
}

void initialize_compass() {
  int16_t x1, y1;
  uint16_t w, h;
  // Heading
  GFXcanvas1 compass_canvas(160, 60);
  compass_canvas.fillScreen(HX8357_BLACK);
  compass_canvas.setFont(&FreeSans9pt7b);
  compass_canvas.drawRect(0, 0, 160, 60, HX8357_YELLOW);
  compass_canvas.getTextBounds("Heading", 0, 12, &x1, &y1, &w, &h);
  compass_canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  compass_canvas.setCursor(0, 12);
  compass_canvas.setTextColor(HX8357_BLACK);
  compass_canvas.print("Heading");
  tft.drawBitmap(0, 81, compass_canvas.getBuffer(), 160, 60, HX8357_YELLOW, HX8357_BLACK);
}

void initialize_pitch() {
  int16_t x1, y1;
  uint16_t w, h;
  // Pitch
  GFXcanvas1 compass_canvas(160, 60);
  compass_canvas.fillScreen(HX8357_BLACK);
  compass_canvas.setFont(&FreeSans9pt7b);
  compass_canvas.drawRect(0, 0, 160, 60, HX8357_YELLOW);
  compass_canvas.getTextBounds("Pitch", 0, 12, &x1, &y1, &w, &h);
  compass_canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  compass_canvas.setCursor(0, 12);
  compass_canvas.setTextColor(HX8357_BLACK);
  compass_canvas.print("Pitch");
  tft.drawBitmap(0, 141, compass_canvas.getBuffer(), 160, 60, HX8357_YELLOW, HX8357_BLACK);
}

void initialize_roll() {
  int16_t x1, y1;
  uint16_t w, h;
  // Roll
  GFXcanvas1 compass_canvas(160, 60);
  compass_canvas.fillScreen(HX8357_BLACK);
  compass_canvas.setFont(&FreeSans9pt7b);
  compass_canvas.drawRect(0, 0, 160, 60, HX8357_YELLOW);
  compass_canvas.getTextBounds("Roll", 0, 12, &x1, &y1, &w, &h);
  compass_canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  compass_canvas.setCursor(0, 12);
  compass_canvas.setTextColor(HX8357_BLACK);
  compass_canvas.print("Roll");
  tft.drawBitmap(0, 201, compass_canvas.getBuffer(), 160, 60, HX8357_YELLOW, HX8357_BLACK);
}

void initialize_stability() {
  int16_t x1, y1;
  uint16_t w, h;
  // Roll
  GFXcanvas1 compass_canvas(160, 59);
  compass_canvas.fillScreen(HX8357_BLACK);
  compass_canvas.setFont(&FreeSans9pt7b);
  compass_canvas.drawRect(0, 0, 160, 59, HX8357_YELLOW);
  compass_canvas.getTextBounds("Stability", 0, 12, &x1, &y1, &w, &h);
  compass_canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  compass_canvas.setCursor(0, 12);
  compass_canvas.setTextColor(HX8357_BLACK);
  compass_canvas.print("Stability");
  tft.drawBitmap(0, 261, compass_canvas.getBuffer(), 160, 59, HX8357_YELLOW, HX8357_BLACK);
}

void initialize_destination() {
  int16_t x1, y1;
  uint16_t w, h;
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
}

void initialize_bearing() {
  int16_t x1, y1;
  uint16_t w, h;
  // // Canvas for Course Correction
  GFXcanvas1 bearing_canvas(159, 110);
  bearing_canvas.fillScreen(HX8357_BLACK);
  bearing_canvas.drawRect(0, 0, 159, 110, HX8357_WHITE);
  bearing_canvas.setFont(&FreeSans9pt7b);
  bearing_canvas.getTextBounds("Bearing", 0, 12, &x1, &y1, &w, &h);
  bearing_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  bearing_canvas.setCursor(0, 14);
  bearing_canvas.setTextColor(HX8357_BLACK);
  bearing_canvas.print("Bearing");
  tft.drawBitmap(161, 106, bearing_canvas.getBuffer(), 159, 110, 0xFC09, HX8357_BLACK);
}

void initialize_volts() {
  int16_t x1, y1;
  uint16_t w, h;
  // // Canvas for Course Correction
  GFXcanvas1 volts_canvas(159, 53);
  volts_canvas.fillScreen(HX8357_BLACK);
  volts_canvas.drawRect(0, 0, 159, 53, HX8357_WHITE);
  volts_canvas.setFont(&FreeSans9pt7b);
  volts_canvas.getTextBounds("Volts", 0, 12, &x1, &y1, &w, &h);
  volts_canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  volts_canvas.setCursor(0, 14);
  volts_canvas.setTextColor(HX8357_BLACK);
  volts_canvas.print("Volts");
  tft.drawBitmap(161, 216, volts_canvas.getBuffer(), 159, 53, HX8357_WHITE, HX8357_BLACK);
}

void initialize_distance() {
  int16_t x1, y1;
  uint16_t w, h;
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
}

void initialize_gps() {
  int16_t x1, y1;
  uint16_t w, h;
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
}

void initialize_date_time() {
  int16_t x1, y1;
  uint16_t w, h;
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
