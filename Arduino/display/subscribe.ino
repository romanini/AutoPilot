#include <WiFi.h>
#include <AsyncUDP.h>

#define DATA_SIZE 300
#define LAST_RECEIVE_MAX_TIME 10000
#define RECONNECT_INTERVAL 30000
#define RECONNECT_WAIT_INTERVAL 30000

static AsyncUDP udp;                   // listens for the controller's telemetry broadcasts
static unsigned int localPort = 8888;  // local port to listen on

// Explicit forward declarations (AsyncUDPPacket isn't visible to Arduino's
// auto-prototype pass at the top of the combined sketch).
void on_udp_packet(AsyncUDPPacket packet);
bool ensure_wifi_connected();

// Written from the AsyncUDP task (on_udp_packet) and read/written from the
// display task (check_subscription); marked volatile for cross-task visibility.
static volatile unsigned long lastReceiveTime = 0;
static volatile bool receiveTimeout = true;
static unsigned long lastConnect = 0;
static unsigned long disconnect_time = 0;

// Called from the AsyncUDP task whenever a telemetry datagram arrives. UDP
// preserves datagram boundaries, so each packet is exactly one complete
// "~APDAT,...$" (or "~RESET,...$") message - no cross-packet reassembly needed.
// autoPilot.parse() is mutex protected, so parsing here (off the display task)
// is safe.
void on_udp_packet(AsyncUDPPacket packet) {
  size_t len = packet.length();
  if (len == 0 || len >= DATA_SIZE) {
    return;  // empty or implausibly large - ignore
  }
  char buf[DATA_SIZE];
  memcpy(buf, packet.data(), len);
  buf[len] = '\0';

  char* start = strchr(buf, '~');  // find the start of the sentence
  if (start == NULL) {
    return;
  }
  start++;                         // move past the '~'
  char* end = strchr(start, '$');  // find the end of the sentence
  if (end == NULL) {
    return;
  }
  *end = '\0';                     // trim the trailing '$'

  autoPilot.parse(start);
  lastReceiveTime = millis();
  receiveTimeout = false;
}

void setup_subscribe() {
  // close() first so this is safe to call again after a WiFi reconnect - it
  // rebinds the listening socket cleanly on the fresh link.
  udp.close();
  if (udp.listen(localPort)) {
    udp.onPacket(on_udp_packet);
    DEBUG_PRINT("UDP telemetry listener started on port ");
    DEBUG_PRINTLN(localPort);
  } else {
    DEBUG_PRINTLN("Failed to start UDP telemetry listener");
  }
  lastConnect = millis();
}

void check_subscription() {
  unsigned long now = millis();

  // AsyncUDP signals packet arrivals but never their absence, so detecting a
  // lost link has to be a polled check here.
  if (!receiveTimeout && (now - lastReceiveTime > LAST_RECEIVE_MAX_TIME)) {
    disconnect_time = now;
    receiveTimeout = true;
    autoPilot.init();
    DEBUG_PRINT("No UDP receives in more than ");
    DEBUG_PRINT(LAST_RECEIVE_MAX_TIME);
    DEBUG_PRINTLN("ms setting receive timeout");
  }

  // While timed out, periodically try to restore WiFi (waiting between attempts
  // so we don't hammer it).
  if (receiveTimeout && (now - disconnect_time > RECONNECT_WAIT_INTERVAL) && (now - lastConnect > RECONNECT_INTERVAL)) {
    DEBUG_PRINTLN("Attempting to restore connection...");
    lastConnect = now;  // throttle retries

    if (ensure_wifi_connected()) {
      DEBUG_PRINTLN("WiFi connection confirmed/re-established. Restarting UDP listener.");
      setup_subscribe();  // re-listen on the fresh link
      receiveTimeout = false;
      lastReceiveTime = millis();
    } else {
      DEBUG_PRINTLN("Failed to re-establish WiFi connection. Will retry later.");
    }
  }
}
