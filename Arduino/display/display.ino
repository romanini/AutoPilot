#include "AutoPilot.h"
#include <WiFi.h>

#define DEBUG_ENABLED 1
#if DEBUG_ENABLED
// Gate every debug write behind `if (Serial)` (USBCDC's operator bool = host
// connected) so we never enter the USB CDC write path while unplugged - that
// path can deadlock on a physical detach, parking the calling task until reset.
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

// FreeRTOS task cores (the Nano ESP32 is dual-core)
#define CORE_0 0
#define CORE_1 1

void display_task(void *pvParameters);
void command_task(void *pvParameters);

#define SETUP_COMPLETE_BEEP_INTERVAL 25
#define DISPLAY_ADDRESS 8
#define DATA_SIZE 300

AutoPilot autoPilot = AutoPilot(&Serial);

char serialzied_data[DATA_SIZE];
int receive_count = 0;

void setup() {
  Serial.begin(38400);
  // USB-CDC serial: never block on TX. Without this, once the TX buffer fills
  // with no host draining it (USB unplugged), the next Serial.print() blocks and
  // stalls the task that called it. With a 0ms timeout, prints are dropped while
  // unplugged and resume cleanly when a laptop is reconnected - no reset needed.
  Serial.setTxTimeoutMs(0);
  DEBUG_PRINTLN("Start");
  setup_screen();
  setup_wifi();
  setup_subscribe();
  setup_button();

  analogSetAttenuation(ADC_11db); // full 3.3V range
  analogReadResolution(12); // explicitly set 12-bit on ESP32

  xTaskCreatePinnedToCore(display_task, "Task Display", 10000, NULL, 1, NULL, CORE_0);
  xTaskCreatePinnedToCore(command_task, "Task Command", 10000, NULL, 2, NULL, CORE_1);
  DEBUG_PRINTLN("Multi-core setup");

  DEBUG_PRINTLN("Setup Complete");
  set_beep(SETUP_COMPLETE_BEEP_INTERVAL);
#ifdef DEBUG_ENABLED
  DEBUG_PRINTLN("Debug Enabled");
#else
  DEBUG_PRINTLN("Debug Disabled");
#endif
}


void loop() {

}

void display_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_voltage();
    display();
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

void command_task(void *pvParameters) {
  for (;;) {  // A Task shall never return or exit.
    check_button();
    check_subscription();
    check_command();
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}