#include <Wire.h>
#include <WiFi.h>
#include "esp_heap_caps.h"
#include "AutoPilot.h"

#define DEBUG_ENABLED 1
#if DEBUG_ENABLED
// Gate every debug write behind `if (Serial)` (USBCDC's operator bool = host
// connected). When USB is unplugged we must NOT enter the CDC write path: on a
// physical detach it can deadlock on an internal USB lock that setTxTimeoutMs()
// does not cover, parking whatever task called print() (e.g. command_task's
// telemetry print) until a reset. Skipping the call entirely while disconnected
// lets the controller run normally headless and resume serial when replugged.
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

// Cross-track (XTE/BOD) steering for Garmin-sourced waypoint nav -- deferred
// Item A (.claude/docs/DeferredWork-PostSeaTrial.md). OFF by default: this is
// a compile-time-only toggle, no runtime switch. Flip to 1, rebuild, and
// reflash to enable; the feature (crosstrack.ino, plus the XTE/BOD parsing in
// garmin.ino) compiles out entirely when 0, so a disabled build carries zero
// trace of it.
#define XTE_STEERING_ENABLED 0

// Low-speed COG fallback for mode 2 (compile-time toggle, same style as
// XTE_STEERING_ENABLED: flip the define, rebuild, reflash).
//
//   1 = fallback active (the on-the-water setting): waypoint nav normally
//       steers course-over-ground, but below the GPS static-nav threshold
//       (PMTK386 in gps.ino, backstopped by the setSpeed() deadband in
//       AutoPilot.cpp) the receiver freezes position AND course - so at low
//       speed the PID would steer against a frozen input and could hold the
//       rudder hard over. Below COG_DROP_SPEED_KNOTS we steer by the compass
//       heading instead; COG comes back at/above COG_MIN_SPEED_KNOTS. The two
//       thresholds are deliberately apart (hysteresis) so the input source
//       doesn't flap when boat speed hovers at the limit.
//       COG_DROP_SPEED_KNOTS must stay >= GPS_SPEED_DEADBAND_KNOTS (0.8,
//       AutoPilot.cpp) - below that the reported speed is snapped to zero, so
//       COG is definitionally untrustworthy.
//
//   0 = old behavior (mode 2 always steers COG, even at speed 0) - the
//       DEFAULT. Also what WORKBENCH builds need: on the bench speed is
//       always 0, so with the fallback active mode 2 would silently steer by
//       compass heading and you could never exercise the COG path.
#define COG_FALLBACK_ENABLED 0
#define COG_MIN_SPEED_KNOTS 1.0f
#define COG_DROP_SPEED_KNOTS 0.8f

#if defined(ARDUINO_ARCH_ESP32)  // Check if the board is based on the ESP32 architecture (like Arduino Nano ESP32)
// Define the cores
#define CORE_0 0
#define CORE_1 1
#define MULTI_CORE true

#if MULTI_CORE
void control_task(void *pvParameters);
void command_task(void *pvParameters);
#endif
#endif

#if XTE_STEERING_ENABLED
// Defined in crosstrack.ino. Returns true and fills *out_heading with a
// leg-course + cross-track-error blended setpoint when live Garmin BOD/XTE
// data is fresh; returns false (leaves *out_heading untouched) otherwise,
// meaning "fall back to plain bearing-to-mark".
bool crosstrack_get_desired_heading(float bearing_to_mark, float distance_nm, float* out_heading);
#endif

AutoPilot autoPilot = AutoPilot(&Serial);

void setup() {
  //Initialize serial
  Serial.begin(38400);
  // USB-CDC serial: never block on TX. Without this, once the TX buffer fills
  // with no host draining it (USB unplugged), the next Serial.print() blocks and
  // stalls the task that called it - which stops telemetry/steering. With a 0ms
  // timeout, prints are simply dropped while unplugged and resume cleanly when a
  // laptop is reconnected, with no reset or recompile needed.
  Serial.setTxTimeoutMs(0);
  DEBUG_PRINTLN("Start");


  Wire.begin();
  setup_wifi();
  setup_publish();
  setup_telnet();
  setup_subscribe();
  setup_motor();
  setup_compass();
  setup_gps();
  setup_garmin();
  setup_pid();
  publish_RESET();

  xTaskCreatePinnedToCore(control_task, "Task Control", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  DEBUG_PRINTLN("Multi-core setup");

  DEBUG_PRINTLN("Setup complete");
#if DEBUG_ENABLED
  DEBUG_PRINTLN("Debug enabled");
#else
  DEBUG_PRINTLN("Debug disabled");
#endif
}

void loop() {

}

void control_task(void *pvParameters) {
  unsigned long last_mills = millis();
  unsigned long cur_mills;
  bool was_navigating = false;
#if COG_FALLBACK_ENABLED
  bool cog_usable = false;  // hysteresis latch for the mode-2 input source
#endif
  float setpoint;
  float input;
  for (;;) {  // A Task shall never return or exit.
    cur_mills = millis();
    check_compass();

    // Advance the PID clock every loop, not just while navigating. If dt were
    // only updated inside the navigating branch it would span the entire time
    // navigation was disabled, spiking the integral the moment we re-engage.
    float diff_time = (cur_mills - last_mills) * 0.001f;
    last_mills = cur_mills;

    bool navigating = autoPilot.isNavigationEndabled();
    if (navigating) {
      // Re-engaging navigation: clear the integral/derivative history so stale
      // windup accumulated before (or during) the disabled period can't kick
      // the rudder.
      if (!was_navigating) {
        reset_pid();
      }
#if COG_FALLBACK_ENABLED
      // Track whether the GPS course is trustworthy (see the
      // COG_FALLBACK_ENABLED comment at the top of the file). Updated every
      // loop so the latch is current the moment mode 2 needs it.
      float speed = autoPilot.getSpeed();
      if (speed >= COG_MIN_SPEED_KNOTS) {
        cog_usable = true;
      } else if (speed < COG_DROP_SPEED_KNOTS) {
        cog_usable = false;
      }
      if (autoPilot.getMode() == 1 || !cog_usable) {
        input = autoPilot.getHeading();
      } else {
        input = autoPilot.getCourse();
      }
#else
      if (autoPilot.getMode() == 1) {
        input = autoPilot.getHeading();
      } else {
        input = autoPilot.getCourse();
      }
#endif
      setpoint = autoPilot.getBearing();
#if XTE_STEERING_ENABLED
      // Blend in cross-track steering only for a live Garmin waypoint nav
      // (mode 2, nav_source == GARMIN == 1 per the AutoPilot.h contract).
      // OpenCPN-sourced waypoints have no XTE/BOD and always steer plain
      // bearing-to-mark.
      if (autoPilot.getMode() == 2 && autoPilot.getNavSource() == 1) {
        float xte_setpoint;
        if (crosstrack_get_desired_heading(setpoint, autoPilot.getDistance(), &xte_setpoint)) {
          setpoint = xte_setpoint;
        }
      }
#endif
      // Compute PID output
      float steer_angle = pid_loop(setpoint, input, diff_time);
      motor_control_loop(steer_angle);
    } else {
      float steer_angle = autoPilot.getSteerAngle();
      motor_control_loop(steer_angle);
    }
    was_navigating = navigating;
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void command_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_telnet();
    check_gps();
    check_garmin();
    navsource_tick();
    publish_APDAT();
    //print_diagnostics();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// TEMPORARY diagnostic: prints memory/stack health once per second so we can
// see whether the controller is leaking heap (or shrinking stack) in the window
// where the display is disconnected, leading up to the telnet 'w' crash. Watch
// "free" trend down and "min"/"largest" approach zero. Remove once Bug A is found.
void print_diagnostics() {
  static uint32_t last_diag = 0;
  if (millis() - last_diag < 1000) {
    return;
  }
  last_diag = millis();
  DEBUG_PRINTF("[DIAG] heap free=%u min_ever=%u largest_block=%u | cmd_task_stack_hw=%u\n",
                (unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size(),
                (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
}


