// Masthead wind sensor for the AutoPilot system.
//
// A port of Norbert Walter's Windsensor Yachta firmware
// (https://github.com/norbert-walter/Windsensor_Yachta) from the ESP8266 to
// the Arduino Nano ESP32, reshaped to fit this project instead of standing on
// its own. The wind maths - cup-anemometer speed, vane angle, Beaufort,
// downwind component - is the original's, and lives in Wind.{h,cpp}; the
// scaffolding around it is new.
//
// What is deliberately NOT ported:
//   - the HTTP server, settings pages, gauges, JSON endpoints and OTA updater.
//     Configuration that used to live on a settings web page is either a
//     #define here or, for the one thing that genuinely has to be set after
//     the head is bolted to the mast, a runtime calibration command (see
//     vane.ino). A phone-facing interface will come back over Bluetooth later.
//   - the NMEA-0183 TCP server. This board is a station on SoberPilot and
//     unicasts ~APWND to the controller over UDP, exactly like the rudder
//     sensor board unicasts ~APRUD - see publish.ino.
//   - the EEPROM configuration blob (Preferences/NVS holds the one persisted
//     value instead) and the ESP8266 hardware-timer plumbing, which the ESP32
//     does not need: micros() in the pulse interrupt measures the rotation
//     directly rather than counting 100us ticks.

#include <Wire.h>
#include "Wind.h"

#define DEBUG_ENABLED 1
#if DEBUG_ENABLED
// Gate every debug write behind `if (Serial)` (USBCDC's operator bool = host
// connected) so we never enter the USB CDC write path while unplugged - that
// path can deadlock on a physical detach, parking the calling task until reset.
// Same reasoning as the controller, display and rudder sketches; it matters
// here for the same reason it does on the rudder board, which is that this one
// spends its whole life headless and unreachable at the top of the mast.
#define DEBUG_PRINT(x) do { if (Serial) Serial.print(x); } while (0)
#define DEBUG_PRINT2(x, y) do { if (Serial) Serial.print(x, y); } while (0)
#define DEBUG_PRINTLN(x) do { if (Serial) Serial.println(x); } while (0)
#define DEBUG_PRINTLN2(x, y) do { if (Serial) Serial.println(x, y); } while (0)
#define DEBUG_PRINTF(...) do { if (Serial) Serial.printf(__VA_ARGS__); } while (0)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINT2(x, y)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTLN2(x, y)
#define DEBUG_PRINTF(...)
#endif

// FreeRTOS task cores (the Nano ESP32 is dual-core), same split the controller,
// display and rudder sketches use: sampling/output on one core, network
// housekeeping and operator commands on the other.
#define CORE_0 0
#define CORE_1 1

// How often the sensors are read and Wind::calculate() runs.
//
// The original recalculates every 500 ms. 100 ms here is a compromise between
// that and the rudder board's 50 Hz: wind is a slow, noisy signal and there is
// no point sampling faster than the anemometer produces pulses (about 11 per
// second in 10 knots), but a wind-vane steering mode - the eventual reason
// this board exists, see .claude/docs/FutureUpgrades-WindAndRudder.md - wants
// something better than two updates a second to steer on.
#define CALCULATE_INTERVAL_MS 100

void sensor_task(void *pvParameters);
void command_task(void *pvParameters);

Wind wind = Wind(&Serial);

static unsigned long lastCalculateTime = 0;

void setup() {
  Serial.begin(115200);
  // USB-CDC serial: never block on TX. Without this, once the TX buffer fills
  // with no host draining it (USB unplugged), the next Serial.print() blocks
  // and stalls the task that called it. With a 0ms timeout, prints are dropped
  // while unplugged and resume cleanly when a laptop is reconnected.
  //
  // Note there is deliberately no `while (!Serial)` wait here: USBCDC's
  // operator bool is "host connected", so waiting on it would hang setup()
  // forever on a headless board - no Wi-Fi, no listener, no sampling.
  Serial.setTxTimeoutMs(0);

  DEBUG_PRINTLN("Masthead wind sensor (Yachta)");

  Wire.begin();

  // setup_vane() creates the mutex both tasks lock through, so it must run
  // before either of them is started.
  setup_vane();
  setup_anemometer();
  setup_temperature();
  setup_wifi();
  setup_subscribe();

  xTaskCreatePinnedToCore(sensor_task, "Task Sensor", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  DEBUG_PRINTLN("Multi-core setup");
#if DEBUG_ENABLED
  DEBUG_PRINTLN("Debug enabled");
#else
  DEBUG_PRINTLN("Debug disabled");
#endif
}

void loop() {
}

// Reads the vane and anemometer, runs the wind maths, and sends ~APWND.
// check_temperature() and publish_wind() gate themselves on their own
// intervals, so the delay below only sets the polling granularity - it is
// deliberately well under every one of those intervals so each cadence lands
// on time instead of being quantised up to 2x. Same arrangement as the rudder
// board's sensor_task and the controller's command_task.
void sensor_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    sample_wind();
    check_temperature();
    publish_wind();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// Network housekeeping plus the deferred vane re-zero, kept off sensor_task's
// core. Both of these block for a while: check_wifi() can sit in an
// association attempt for up to WIFI_ATTEMPT_TIMEOUT_MS, and a calibration
// does an NVS flash write. On their own core, neither stalls sampling - and in
// particular a reconnect attempt can't swallow the anemometer's pulse timing.
void command_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_wifi();
    check_calibration_request();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// One pass of "read the sensors, do the maths", gated to CALCULATE_INTERVAL_MS.
// Temperature is not read here - it is on its own slower cadence, because the
// DS18B20's self-heating compensation depends on how often it is polled (see
// temperature.ino).
void sample_wind() {
  if (millis() - lastCalculateTime < CALCULATE_INTERVAL_MS) {
    return;
  }
  lastCalculateTime = millis();

  float degrees;
  uint16_t magnitude;
  bool vaneOk;
  read_vane(&degrees, &magnitude, &vaneOk);
  wind.setVane(degrees, magnitude, vaneOk);

  float periodMs;
  bool rotating;
  read_anemometer(&periodMs, &rotating);
  wind.setRotationPeriod(periodMs, rotating);

  wind.calculate();
}
