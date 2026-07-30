// FreeRTOS task cores (the Nano ESP32 is dual-core), same split the controller
// and display sketches use: sampling/output on one core, network housekeeping
// and operator commands on the other.
#define CORE_0 0
#define CORE_1 1

void sensor_task(void *pvParameters);
void command_task(void *pvParameters);

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);

  Serial.println("Rudder position sensor");

  // setup_angle() creates the mutex the two tasks lock through, so it must run
  // before either of them is started.
  setup_angle();
  setup_wifi();
  setup_subscribe();

  xTaskCreatePinnedToCore(sensor_task, "Task Sensor", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  Serial.println("Multi-core setup");
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
