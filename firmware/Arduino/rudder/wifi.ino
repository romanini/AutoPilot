#include <WiFi.h>
#include "arduino_secrets.h"

// The rudder board lives headless at the rudder stock, so it must never need a
// power cycle to rejoin the AP. Two cases are handled: SoberPilot not up yet at
// boot (the whole boat just got power and the controller is still coming up),
// and the link dropping later. See setup_wifi() and check_wifi() below.
#define WIFI_ATTEMPT_TIMEOUT_MS 8000  // how long one association attempt gets
#define WIFI_RETRY_INTERVAL_MS 5000   // throttle between reconnect attempts
#define WIFI_SETUP_ATTEMPTS 3         // boot-time attempts before deferring to loop()

char ssid[] = "SoberPilot";
char pass[] = SECRET_PASS;

static unsigned long lastWifiAttempt = 0;
static bool wifiWasConnected = false;

// One association attempt: returns as soon as the link is up, or gives up after
// WIFI_ATTEMPT_TIMEOUT_MS. Blocking here is fine - with no link there is
// nothing to publish anyway, and the command listener is an AsyncUDP callback
// on its own task, so a relayed ~APCMD,z$ is still serviced during this wait.
static bool try_connect_wifi() {
  DEBUG_PRINT("Connecting to ");
  DEBUG_PRINTLN(ssid);
  WiFi.begin(ssid, pass);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < WIFI_ATTEMPT_TIMEOUT_MS) {
    delay(250);
    DEBUG_PRINT(".");
  }
  DEBUG_PRINTLN();

  if (WiFi.status() == WL_CONNECTED) {
    DEBUG_PRINT("Connected, IP: ");
    DEBUG_PRINTLN(WiFi.localIP());
    return true;
  }
  DEBUG_PRINTLN("Connect attempt timed out");
  return false;
}

void setup_wifi() {
  WiFi.mode(WIFI_STA);
  // Belt and braces alongside check_wifi(): let the SDK re-associate on its own
  // when it can, and keep the radio out of powersave. Powersave is what made
  // the navigator's wlan0 miss the controller's broadcasts (see
  // navigator/README.md); here it would cost us relayed commands and add
  // latency to the 50 Hz telemetry stream. The board is 12 V powered, so the
  // extra draw doesn't matter.
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);

  for (int attempt = 0; attempt < WIFI_SETUP_ATTEMPTS; attempt++) {
    if (try_connect_wifi()) {
      wifiWasConnected = true;
      break;
    }
  }
  lastWifiAttempt = millis();

  if (!wifiWasConnected) {
    // Deliberately fall through rather than spin here: blocking in setup()
    // until the AP appears would leave the board wedged with the command
    // listener never started, needing a power cycle once SoberPilot came up.
    DEBUG_PRINTLN("No SoberPilot at startup - command_task will keep retrying");
  }
}

// Polled from command_task (rudder.ino) - deliberately not from the sampling
// task, since try_connect_wifi() below blocks for up to
// WIFI_ATTEMPT_TIMEOUT_MS. Covers "never came up at boot" and "was up, then
// the AP went away", and on every fresh link re-binds the UDP command listener -
// the socket does not survive the link going down, so without this the board
// would publish fine but silently stop accepting ~APCMD,z$ (same reason
// display/subscribe.ino re-calls setup_subscribe() on its reconnect path).
void check_wifi() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      // Came back on its own (setAutoReconnect) rather than via our retry below.
      wifiWasConnected = true;
      DEBUG_PRINT("WiFi reconnected, IP: ");
      DEBUG_PRINTLN(WiFi.localIP());
      setup_subscribe();
    }
    return;
  }

  if (wifiWasConnected) {
    wifiWasConnected = false;
    DEBUG_PRINTLN("WiFi link lost");
  }

  if (millis() - lastWifiAttempt < WIFI_RETRY_INTERVAL_MS) {
    return;  // throttled - don't hammer the radio
  }
  lastWifiAttempt = millis();

  DEBUG_PRINTLN("Attempting to rejoin SoberPilot...");
  WiFi.disconnect(true);  // clean slate for the retry, as display/wifi.ino does
  delay(100);
  if (try_connect_wifi()) {
    wifiWasConnected = true;
    setup_subscribe();
  }
}
