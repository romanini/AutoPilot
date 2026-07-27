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

// Same lavender/magenta used everywhere the old "Destination" box used to draw
// (Mode text, Target/Correction, and previously Follow) - kept as one named
// constant since the Target box now carries all of that meaning.
static const uint16_t MAGENTA = 0xF57F;

Adafruit_HX8357 tft = Adafruit_HX8357(&SPI, TFT_CS, TFT_DC, TFT_RST);

uint16_t backgroundColor = HX8357_BLACK;

// Tracks the last value painted for each display item.
// Initialized to sentinels in initialize_displayed_values() so everything
// paints on the first call to display().
struct {
  float speed;
  bool  speed_hasFix;

  float heading;
  bool  heading_connected;

  float track;
  bool  track_valid;

  // Mode box now folds in nav_source (who's steering), which the controller
  // can change without the mode value itself changing (e.g. Garmin -> OpenCPN,
  // or a live source going stale back to NONE while mode stays 2). None of
  // that flips the one-shot hasModeChanged() flag, so this box is polled every
  // loop like Follow used to be, instead of being event-driven.
  bool connected_mode;
  bool navEnabled_mode;
  bool tack_mode;
  int  mode_mode;
  int  navSource_mode;

  float target;
  bool  target_connected;
  bool  target_navEnabled;

  float correction;
  bool  correction_connected;
  bool  correction_navEnabled;

  float distance;
  bool  distance_waypointSet;
  bool  distance_hasFix;

  float locationLat;
  bool  lat_hasFix;

  float locationLon;
  bool  lon_hasFix;

  float waypointLat;
  bool  waypointLat_set;

  float waypointLon;
  bool  waypointLon_set;

  int  dtMonth, dtDay, dtYear, dtHour, dtMinute;
  bool dt_hasFix;

  int  fixquality;
  int  satellites;
  bool fix_hasFix;

  float batteryVoltage;
  float inputVoltage;
} disp;

void initialize_displayed_values() {
  disp.speed = -999.0f;          disp.speed_hasFix = false;
  disp.heading = -999.0f;        disp.heading_connected = false;
  disp.track = -999.0f;          disp.track_valid = false;

  disp.connected_mode = false;   disp.navEnabled_mode = false; disp.tack_mode = false;
  disp.mode_mode = -1;           disp.navSource_mode = -1;

  disp.target = -999.0f;         disp.target_connected = false; disp.target_navEnabled = false;
  disp.correction = -999.0f;     disp.correction_connected = false; disp.correction_navEnabled = false;

  disp.distance = -999.0f;       disp.distance_waypointSet = false; disp.distance_hasFix = false;
  disp.locationLat = -999.0f;    disp.lat_hasFix = false;
  disp.locationLon = -999.0f;    disp.lon_hasFix = false;
  disp.waypointLat = -999.0f;    disp.waypointLat_set = false;
  disp.waypointLon = -999.0f;    disp.waypointLon_set = false;

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
  display_speed();
  display_heading();
  display_track();
  display_mode();
  display_target();
  display_correction();
  display_distance();
  display_location_lat();
  display_location_lon();
  display_waypoint_lat();
  display_waypoint_lon();
  display_datetime();
  display_fix();
  display_volts();
}

void display_heading() {
  float cur_heading   = autoPilot.getHeading();
  bool  cur_connected = autoPilot.isConnected();
  if (cur_heading == disp.heading && cur_connected == disp.heading_connected) return;
  disp.heading           = cur_heading;
  disp.heading_connected = cur_connected;

  GFXcanvas1 canvas(130, 44);
  uint16_t foregroundColor = HX8357_YELLOW;
  canvas.fillScreen(0);

  if (cur_connected) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold24pt7b);
    canvas.setCursor(0, 37);
    canvas.print(cur_heading);
  }
  tft.drawBitmap(20, 30, canvas.getBuffer(), 130, 44, foregroundColor, backgroundColor);
}

void display_speed() {
  float cur_speed  = autoPilot.getSpeed();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_speed == disp.speed && cur_hasFix == disp.speed_hasFix) return;
  disp.speed      = cur_speed;
  disp.speed_hasFix = cur_hasFix;

  GFXcanvas1 canvas(130, 44);
  uint16_t foregroundColor = HX8357_CYAN;
  canvas.fillScreen(0);

  if (cur_hasFix) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold24pt7b);
    canvas.setCursor(0, 37);
    canvas.print(cur_speed, 2);
  }
  tft.drawBitmap(20, 119, canvas.getBuffer(), 130, 44, foregroundColor, backgroundColor);
}

void display_track() {
  // Damped, trust-gated GPS track - not raw course, which is known-noisy
  // below ~1kn (see AutoPilot.cpp's COG_TRUST_ENTER/EXIT_KNOTS). Blanks
  // instead of showing a value until isDampedCourseValid() is true.
  float cur_track = autoPilot.getDampedCourse();
  bool  cur_valid = autoPilot.isDampedCourseValid();
  if (cur_track == disp.track && cur_valid == disp.track_valid) return;
  disp.track        = cur_track;
  disp.track_valid = cur_valid;

  GFXcanvas1 canvas(130, 44);
  uint16_t foregroundColor = HX8357_CYAN;
  canvas.fillScreen(0);

  if (cur_valid) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold24pt7b);
    canvas.setCursor(0, 37);
    canvas.print(cur_track, 1);
  }
  tft.drawBitmap(20, 208, canvas.getBuffer(), 130, 44, foregroundColor, backgroundColor);
}

// Polled every loop rather than gated on hasModeChanged(): nav_source can
// change (Garmin <-> OpenCPN <-> NONE) without the numeric mode value itself
// changing, which is exactly the "was following Garmin, source went stale,
// now circling the last waypoint" case we most want this box to catch.
void display_mode() {
  bool cur_connected  = autoPilot.isConnected();
  bool cur_navEnabled = autoPilot.isNavigationEnabled();
  bool cur_tack       = autoPilot.isTackRequested();
  int  cur_mode       = autoPilot.getMode();
  int  cur_navSource  = autoPilot.getNavSource();

  if (cur_connected == disp.connected_mode && cur_navEnabled == disp.navEnabled_mode &&
      cur_tack == disp.tack_mode && cur_mode == disp.mode_mode && cur_navSource == disp.navSource_mode) {
    return;
  }
  disp.connected_mode  = cur_connected;
  disp.navEnabled_mode = cur_navEnabled;
  disp.tack_mode       = cur_tack;
  disp.mode_mode       = cur_mode;
  disp.navSource_mode  = cur_navSource;

  GFXcanvas1 canvas(135, 24);
  canvas.fillScreen(0);
  canvas.setTextColor(1);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(0, 18);

  if (!cur_connected) {
    canvas.print("no link");
  } else if (cur_navEnabled) {
    if (cur_tack) {
      canvas.print("tack");
    } else if (cur_mode == 2) {
      switch (cur_navSource) {
             //  canvas.print("no link");
             //  canvas.print("compass");
             //  canvas.print("disabled");
        case 1:  canvas.print("garmin");   break;
        case 2:  canvas.print("opencpn");  break;
        default: canvas.print("waypoint"); break;
      }
    } else if (cur_mode == 1) {
      canvas.print("compass");
    }
  } else {
    canvas.print("disabled");
  }
  tft.drawBitmap(176, 38, canvas.getBuffer(), 135, 24, MAGENTA, backgroundColor);
}

// Target = the live compass setpoint the autopilot is steering to: the held
// heading in mode 1, or the GPS-track-trimmed steering setpoint in mode 2
// (AutoPilot::heading_command on the controller) - publish.ino already picks
// the right one for this same wire field, so no mode check is needed here.
void display_target() {
  bool  cur_connected  = autoPilot.isConnected();
  bool  cur_navEnabled = autoPilot.isNavigationEnabled();
  float cur_target     = autoPilot.getHeadingDesired();
  if (cur_target == disp.target && cur_connected == disp.target_connected &&
      cur_navEnabled == disp.target_navEnabled) return;
  disp.target          = cur_target;
  disp.target_connected  = cur_connected;
  disp.target_navEnabled = cur_navEnabled;

  GFXcanvas1 canvas(130, 44);
  canvas.fillScreen(0);
  if (cur_connected && cur_navEnabled) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold24pt7b);
    canvas.setCursor(0, 37);
    canvas.print(cur_target, 1);
  }
  tft.drawBitmap(181, 130, canvas.getBuffer(), 130, 44, MAGENTA, backgroundColor);
}

// Correction lives in the same box as Target, right underneath it, with no
// label of its own - the only "unit" kept is the L/R steer direction.
void display_correction() {
  bool  cur_connected  = autoPilot.isConnected();
  bool  cur_navEnabled = autoPilot.isNavigationEnabled();
  float cur_correction = autoPilot.getBearingCorrection();
  if (cur_correction == disp.correction && cur_connected == disp.correction_connected &&
      cur_navEnabled == disp.correction_navEnabled) return;
  disp.correction          = cur_correction;
  disp.correction_connected  = cur_connected;
  disp.correction_navEnabled = cur_navEnabled;

  GFXcanvas1 canvas(135, 32);
  canvas.fillScreen(0);
  if (cur_connected && cur_navEnabled) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold18pt7b);
    canvas.setCursor(0, 29);
    canvas.print((cur_correction > 0) ? cur_correction : cur_correction * -1.0, 1);
    canvas.println((cur_correction > 0) ? " R" : " L");
  }
  tft.drawBitmap(182, 182, canvas.getBuffer(), 135, 32, MAGENTA, backgroundColor);
}

void display_volts() {
  float cur_batt  = autoPilot.getBatteryVoltage();
  float cur_input = autoPilot.getInputVoltage();
  if (cur_batt == disp.batteryVoltage && cur_input == disp.inputVoltage) return;
  disp.batteryVoltage = cur_batt;
  disp.inputVoltage   = cur_input;

  GFXcanvas1 canvas(120, 22);
  uint16_t foregroundColor = HX8357_WHITE;
  canvas.fillScreen(0);

  canvas.setTextColor(1);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(0, 18);
  canvas.print(cur_batt, 2);
  canvas.print(" / ");
  canvas.println(cur_input, 1);
  tft.drawBitmap(20, 289, canvas.getBuffer(), 120, 22, foregroundColor, backgroundColor);
}

void display_distance() {
  float cur_distance    = autoPilot.getDistance();
  bool  cur_waypointSet = autoPilot.isWaypointSet();
  bool  cur_hasFix      = autoPilot.hasFix();
  if (cur_distance == disp.distance && cur_waypointSet == disp.distance_waypointSet && cur_hasFix == disp.distance_hasFix) return;
  disp.distance            = cur_distance;
  disp.distance_waypointSet = cur_waypointSet;
  disp.distance_hasFix     = cur_hasFix;

  GFXcanvas1 canvas(130, 44);
  uint16_t foregroundColor = HX8357_CYAN;
  canvas.fillScreen(0);

  if (cur_waypointSet && cur_hasFix) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold24pt7b);
    canvas.setCursor(0, 37);
    canvas.print(cur_distance, 2);
  }
  tft.drawBitmap(341, 119, canvas.getBuffer(), 130, 44, foregroundColor, backgroundColor);
}

void display_location_lat() {
  float cur_lat    = autoPilot.getLocationLat();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_lat == disp.locationLat && cur_hasFix == disp.lat_hasFix) return;
  disp.locationLat = cur_lat;
  disp.lat_hasFix  = cur_hasFix;

  GFXcanvas1 canvas(115, 22);
  uint16_t foregroundColor = 0x7FE8;
  canvas.fillScreen(0);

  if (cur_hasFix) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(0, 18);
    canvas.print(cur_lat, 6);
  }
  tft.drawBitmap(341, 22, canvas.getBuffer(), 115, 22, foregroundColor, backgroundColor);
}

void display_location_lon() {
  float cur_lon    = autoPilot.getLocationLon();
  bool  cur_hasFix = autoPilot.hasFix();
  if (cur_lon == disp.locationLon && cur_hasFix == disp.lon_hasFix) return;
  disp.locationLon = cur_lon;
  disp.lon_hasFix  = cur_hasFix;

  GFXcanvas1 canvas(139, 22);
  uint16_t foregroundColor = 0x7FE8;
  canvas.fillScreen(0);

  if (cur_hasFix) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(0, 18);
    canvas.print(cur_lon, 6);
  }
  tft.drawBitmap(331, 46, canvas.getBuffer(), 139, 22, foregroundColor, backgroundColor);
}

// Mirrors display_location_lat/lon exactly (same font, same two-line layout)
// but for the commanded waypoint, and only while one is actually set.
void display_waypoint_lat() {
  float cur_lat = autoPilot.getWaypointLat();
  bool  cur_set = autoPilot.isWaypointSet();
  if (cur_lat == disp.waypointLat && cur_set == disp.waypointLat_set) return;
  disp.waypointLat     = cur_lat;
  disp.waypointLat_set = cur_set;

  GFXcanvas1 canvas(115, 22);
  uint16_t foregroundColor = 0x7FE8;
  canvas.fillScreen(0);

  if (cur_set) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(0, 18);
    canvas.print(cur_lat, 6);
  }
  tft.drawBitmap(341, 200, canvas.getBuffer(), 115, 22, foregroundColor, backgroundColor);
}

void display_waypoint_lon() {
  float cur_lon = autoPilot.getWaypointLon();
  bool  cur_set = autoPilot.isWaypointSet();
  if (cur_lon == disp.waypointLon && cur_set == disp.waypointLon_set) return;
  disp.waypointLon     = cur_lon;
  disp.waypointLon_set = cur_set;

  GFXcanvas1 canvas(139, 22);
  uint16_t foregroundColor = 0x7FE8;
  canvas.fillScreen(0);

  if (cur_set) {
    canvas.setTextColor(1);
    canvas.setFont(&FreeSansBold12pt7b);
    canvas.setCursor(0, 18);
    canvas.print(cur_lon, 6);
  }
  tft.drawBitmap(331, 224, canvas.getBuffer(), 139, 22, foregroundColor, backgroundColor);
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

  GFXcanvas1 canvas(165, 22);
  uint16_t foregroundColor = HX8357_WHITE;
  canvas.fillScreen(0);

  canvas.setTextColor(1);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(0, 18);
  if (cur_hasFix) {
    char dateTimeString[16];
    sprintf(dateTimeString, "%d/%d/%02d %d:%02d", cur_month, cur_day, cur_year % 100, cur_hour, cur_minute);
    canvas.print(dateTimeString);
  }
  tft.drawBitmap(181, 289, canvas.getBuffer(), 165, 22, foregroundColor, backgroundColor);
}

void display_fix() {
  int  cur_fixquality = autoPilot.getFixquality();
  int  cur_satellites = autoPilot.getSatellites();
  bool cur_hasFix     = autoPilot.hasFix();
  if (cur_fixquality == disp.fixquality && cur_satellites == disp.satellites && cur_hasFix == disp.fix_hasFix) return;
  disp.fixquality = cur_fixquality;
  disp.satellites = cur_satellites;
  disp.fix_hasFix = cur_hasFix;

  GFXcanvas1 canvas(110, 22);
  uint16_t foregroundColor = HX8357_WHITE;
  canvas.fillScreen(0);

  canvas.setTextColor(1);
  canvas.setFont(&FreeSansBold12pt7b);
  canvas.setCursor(0, 18);
  if (cur_hasFix) {
    if (cur_fixquality == 0) {
      canvas.print("n/a");
    } else if (cur_fixquality == 1) {
      canvas.print("GPS");
    } else if (cur_fixquality == 2) {
      canvas.print("DGPS");
    } else {
      // if it is not 1 or 2 let's display it so we can figure out what it is.
      canvas.print(cur_fixquality);
    }
    canvas.print("(");
    canvas.print(cur_satellites);
    canvas.print(")");
  }
  tft.drawBitmap(360, 289, canvas.getBuffer(), 110, 22, foregroundColor, backgroundColor);
}

void initialize_display() {
  DEBUG_PRINTLN("Initializing display");
  initialize_heading();
  initialize_speed();
  initialize_track();
  initialize_mode();
  initialize_target();
  initialize_location();
  initialize_distance();
  initialize_waypoint();
  initialize_volts();
  initialize_date_time();
}

// Row heights: the bottom bar is back to its original 54px (matching the old
// standalone Volts box), so Heading/Speed/Track (and everything else in rows
// 1-3) pick up the freed height, split proportionately (89/89/88).
// Column 1 (x 0-160): Heading / Speed / Track.
void initialize_heading() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(160, 89);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 160, 89, HX8357_YELLOW);
  canvas.getTextBounds("Heading", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_YELLOW);
  canvas.setCursor(0, 12);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Heading");
  tft.drawBitmap(0, 0, canvas.getBuffer(), 160, 89, HX8357_YELLOW, HX8357_BLACK);
}

void initialize_speed() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(160, 89);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 160, 89, HX8357_CYAN);
  canvas.getTextBounds("Speed", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_CYAN);
  canvas.setCursor(0, 12);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Speed");
  tft.drawBitmap(0, 89, canvas.getBuffer(), 160, 89, HX8357_CYAN, HX8357_BLACK);
}

void initialize_track() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(160, 88);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 160, 88, HX8357_CYAN);
  canvas.getTextBounds("Track", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_CYAN);
  canvas.setCursor(0, 12);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Track");
  tft.drawBitmap(0, 178, canvas.getBuffer(), 160, 88, HX8357_CYAN, HX8357_BLACK);
}

// Column 2 (x 161-320): Mode (top row), Target+Correction (spans the next two
// rows and holds both values in one box, no label on Correction).
void initialize_mode() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(159, 89);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 159, 89, HX8357_WHITE);
  canvas.getTextBounds("Mode", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_WHITE);
  canvas.setCursor(0, 12);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Mode");
  tft.drawBitmap(161, 0, canvas.getBuffer(), 159, 89, MAGENTA, HX8357_BLACK);
}

void initialize_target() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(159, 177);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 159, 177, HX8357_WHITE);
  canvas.getTextBounds("Target", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 1, HX8357_WHITE);
  canvas.setCursor(0, 12);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Target");
  tft.drawBitmap(161, 89, canvas.getBuffer(), 159, 177, MAGENTA, HX8357_BLACK);
}

// Column 3 (x 321-480): Location (top), Distance, Waypoint.
void initialize_location() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(159, 89);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 159, 89, HX8357_YELLOW);
  canvas.getTextBounds("Location", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_YELLOW);
  canvas.setCursor(0, 14);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Location");
  tft.drawBitmap(321, 0, canvas.getBuffer(), 159, 89, 0x7FE8, HX8357_BLACK);
}

void initialize_distance() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(159, 89);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 159, 89, HX8357_WHITE);
  canvas.getTextBounds("Distance", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  canvas.setCursor(0, 14);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Distance");
  tft.drawBitmap(321, 89, canvas.getBuffer(), 159, 89, HX8357_CYAN, HX8357_BLACK);
}

void initialize_waypoint() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(159, 88);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 159, 88, HX8357_YELLOW);
  canvas.getTextBounds("Waypoint", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_YELLOW);
  canvas.setCursor(0, 14);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Waypoint");
  tft.drawBitmap(321, 178, canvas.getBuffer(), 159, 88, 0x7FE8, HX8357_BLACK);
}

// Bottom bar (y 266-320, 54px - back to the original Volts/Date-Time height):
// Volts is the left third (column 1's width), Date/Time + fix/satellites is
// the right two-thirds (columns 2+3), unchanged content and font from before.
void initialize_volts() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(160, 54);
  canvas.fillScreen(HX8357_BLACK);
  canvas.drawRect(0, 0, 160, 54, HX8357_WHITE);
  canvas.setFont(&FreeSans9pt7b);
  canvas.getTextBounds("Volts", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  canvas.setCursor(0, 14);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Volts");
  tft.drawBitmap(0, 266, canvas.getBuffer(), 160, 54, HX8357_WHITE, HX8357_BLACK);
}

void initialize_date_time() {
  int16_t x1, y1;
  uint16_t w, h;
  GFXcanvas1 canvas(319, 54);
  canvas.fillScreen(HX8357_BLACK);
  canvas.setFont(&FreeSans9pt7b);
  canvas.drawRect(0, 0, 319, 54, HX8357_WHITE);
  canvas.getTextBounds("Date &Time", 0, 12, &x1, &y1, &w, &h);
  canvas.fillRect(x1, y1, w + 8, h + 3, HX8357_WHITE);
  canvas.setCursor(0, 14);
  canvas.setTextColor(HX8357_BLACK);
  canvas.print("Date & Time");
  tft.drawBitmap(161, 266, canvas.getBuffer(), 319, 54, HX8357_WHITE, HX8357_BLACK);
}
