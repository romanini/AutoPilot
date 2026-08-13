#define DEBUG_ENABLED 1
#if DEBUG_ENABLED
// Gate every debug write behind `if (Serial)` (USBCDC's operator bool = host
// connected) so we never enter the USB CDC write path while unplugged - that
// path can deadlock on a physical detach, parking the calling task until reset.
// This board matters more than most: it runs permanently headless in a sealed
// enclosure at the rudder stock, and publish_rudder() prints at 50 Hz.
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

// FreeRTOS task cores (the Nano ESP32 is dual-core), same split the controller
// and display sketches use: sampling/output on one core, network housekeeping
// and operator commands on the other.
#define CORE_0 0
#define CORE_1 1

void sensor_task(void *pvParameters);
void command_task(void *pvParameters);

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

  DEBUG_PRINTLN("Rudder position sensor");

  // setup_angle() creates the mutex the two tasks lock through, so it must run
  // before either of them is started.
  setup_angle();
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

// Reads the AS5600 and sends ~APRUD. publish_rudder() gates itself on
// PUBLISH_INTERVAL_MS, so the delay here only sets the polling granularity -
// deliberately well under the publish interval so the 50 Hz cadence lands on
// time instead of being quantised up to 2x. Same arrangement as the
// controller's command_task vs publish_APDAT().
void sensor_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    publish_rudder();
    vTaskDelay(5 / portTICK_PERIOD_MS);
  }
}

// Network housekeeping plus the deferred re-zero, kept off sensor_task's core.
// Both of these block for a while: check_wifi() can sit in an association
// attempt for up to WIFI_ATTEMPT_TIMEOUT_MS, and a calibration does an NVS
// flash write. On their own core, neither stalls the 50 Hz sampling - a
// reconnect attempt no longer costs us 8 seconds of rudder angle the way it did
// when this ran from loop().
void command_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_wifi();
    check_calibration_request();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
