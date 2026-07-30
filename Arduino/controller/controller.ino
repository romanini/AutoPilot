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
  setup_rudder();
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

    int autotune_state = autoPilot.getAutoTuneState();
    if (autotune_state == 2) {
      // Auto-tuning (autotune.ino) owns the wheel this tick - skip the normal
      // navigate/manual-steer path entirely until it finishes or is aborted.
      autotune_loop(diff_time);
      was_navigating = false;
      vTaskDelay(10 / portTICK_PERIOD_MS);
      continue;
    }
    if (autotune_state == 1) {
      autotune_check_ready_timeout();
    }

    bool navigating = autoPilot.isNavigationEndabled();
    if (navigating) {
      // Re-engaging navigation: clear the integral/derivative history so stale
      // windup accumulated before (or during) the disabled period can't kick
      // the rudder.
      if (!was_navigating) {
        reset_pid();
      }
      // Option 3: the inner loop always steers by the compass - it's rock
      // steady at 100 Hz, unlike raw/damped GPS course at ~1 Hz. Mode 1's
      // setpoint is the operator's compass heading; mode 2's setpoint is
      // heading_command, a magnetic-frame value that gpstracktrim.ino trims
      // slowly (see gpstracktrim.ino) to keep the GPS track on the bearing
      // (or XTE-corrected course) to the waypoint. GPS noise never reaches
      // this loop directly.
      input = autoPilot.getHeading();
      setpoint = (autoPilot.getMode() == 1) ? autoPilot.getBearing() : autoPilot.getHeadingCommand();
      // Compute PID output
      float steer_angle = pid_loop(setpoint, input, diff_time);
      motor_control_loop(steer_angle);
    } else {
      // Navigation just disengaged: freeze the wheel where it is. The PID had
      // been holding standing helm (the virtual rudder position is well away
      // from zero), while steer_angle is whatever it last was - usually 0. If
      // we steered toward it here, Disable would unwind all that standing
      // rudder in one hard multi-second turn. Adopting the current position as
      // the manual setpoint makes Disable a no-op for the wheel; telnet manual
      // steering still works from there because it sets steer_angle absolutely.
      if (was_navigating) {
        autoPilot.setSteerAngle(motor_position_degrees());
        move_motor(0);
      }
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
    gps_track_trim();
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


