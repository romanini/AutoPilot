#include <HardwareSerial.h>

#define DEBUG_GARMIN 1
#define GARMIN_MAX_CHARS_PER_LINE 120   // bail out of a line if it runs long, rather than spinning
#define GARMIN_MAX_READ_MILLIS 100        // hard cap on time spent per port per call so we don't starve other tasks (e.g. telnet)

// Pin names are from the GARMIN's perspective: GARMIN_TX_x is the controller pin
// the Garmin's TX wire lands on (= the controller's UART RX); GARMIN_RX_x is where
// the Garmin's RX wire lands (= the controller's UART TX). begin() is (baud, cfg,
// rxPin, txPin), so begin(.., GARMIN_TX_x, GARMIN_RX_x) wires rx=Garmin-TX,
// tx=Garmin-RX. Per the schematic (circuit/Controller/Controller/), confirmed by
// measurement 2026-06-28: "Garmin In" JST pin1 Tx/A -> Transmit Buffer -> ADC0
// (= A0, the controller RX); pin2 Rx/A -> Receive Buffer -> ADC1 (= A1, the
// controller TX); likewise ADC2/ADC3 for channel B.
#define GARMIN_TX_A A0   // Garmin TX (chan A) -> controller RX  (ADC0)
#define GARMIN_RX_A A1   // Garmin RX (chan A) <- controller TX  (ADC1)
#define GARMIN_TX_B A2   // Garmin TX (chan B) -> controller RX  (ADC2)
#define GARMIN_RX_B A3   // Garmin RX (chan B) <- controller TX  (ADC3)

// Outbound (controller -> Garmin) line FIFO. ~APTX frames from the plugin land
// here (subscribe.ino) and are drained a few per command_task iteration so a
// whole-route burst doesn't blow the FreeRTOS time budget. Producer is the
// AsyncUDP task; consumer is command_task -> a thread-safe queue is required.
#define GARMIN_TX_QUEUE_LEN 32
#define GARMIN_TX_LINE_MAX 100          // one NMEA sentence (<=82) fits comfortably
#define GARMIN_TX_DRAIN_PER_CALL 4      // lines written to the UART per check_garmin()

// Create two extra HardwareSerial ports
//   UART_NUM_1  → "Serial1"
//   UART_NUM_2  → "Serial2"
HardwareSerial Serial1Port(1);
HardwareSerial Serial2Port(2);

// Persistent per-port line-assembly state. check_garmin() runs on a 100 ms
// budget, so a sentence split across two calls must survive between calls --
// these buffers accumulate chars until a CR/LF completes a line.
struct GarminPort {
  char buf[GARMIN_MAX_CHARS_PER_LINE + 1];
  int  len;
  bool overflow;   // current line too long; discard chars until the next CR/LF
};
static GarminPort gpA = { {0}, 0, false };
static GarminPort gpB = { {0}, 0, false };

typedef struct {
  char line[GARMIN_TX_LINE_MAX];
} GarminTxItem;
static QueueHandle_t garmin_tx_queue = NULL;

// Defined in publish.ino -- broadcast a received Garmin line as "~APRX,...$".
void publish_APRX(const char* nmea);
// Defined in navsource.ino -- feed a parsed RMB into the GARMIN nav source (step 4).
void navsource_garmin_rmb(char status, double dest_lat, double dest_lon, bool have_pos, char arrival);

void setup_garmin() {

  //Serial1 on 9,600 baud noote that the pins are revered because we want the Garmin TX to connect to an RX pin
  // and the Garmin RX to connect to a TX pin.  The  being() function parsm are in order of RX then TX
  Serial1Port.begin(4800, SERIAL_8N1, GARMIN_TX_A, GARMIN_RX_A);

  //Serial2 on 9,600 baud noote that the pins are revered because we want the Garmin TX to connect to an RX pin
  // and the Garmin RX to connect to a TX pin.  The  being() function parsm are in order of RX then TX
  Serial2Port.begin(4800, SERIAL_8N1, GARMIN_TX_B, GARMIN_RX_B);

  garmin_tx_queue = xQueueCreate(GARMIN_TX_QUEUE_LEN, sizeof(GarminTxItem));

  DEBUG_PRINTLN("Garmin A and B setup");
}

// Verify a "$....*HH" NMEA checksum: XOR of all chars between '$' and '*', vs.
// the two hex digits after '*'. Lines without a "*HH" are rejected (our relayed
// set -- WPL/RTE/RMB/XTE/BOD -- always carry one).
static bool nmea_checksum_ok(const char* line) {
  if (line[0] != '$') return false;
  const char* p = line + 1;
  uint8_t cs = 0;
  while (*p && *p != '*') {
    cs ^= (uint8_t)*p;
    p++;
  }
  if (*p != '*' || !isxdigit((unsigned char)p[1]) || !isxdigit((unsigned char)p[2])) {
    return false;  // no/short checksum
  }
  auto hexval = [](char c) -> uint8_t {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return (c - 'a' + 10);
  };
  uint8_t given = (hexval(p[1]) << 4) | hexval(p[2]);
  return given == cs;
}

// Relay only the route/nav sentences (§4 of the wire contract). Match on the
// 3-letter type at offset 3 ("$ttSSS,...") so any talker ID passes. Drops the
// high-rate RMC/GGA/GLL/VTG so the UDP broadcast isn't flooded.
static bool garmin_should_relay(const char* line) {
  if (line[0] != '$' || strlen(line) < 6) return false;
  const char* t = line + 3;  // skip '$' + 2-char talker
  return strncmp(t, "WPL", 3) == 0
      || strncmp(t, "RTE", 3) == 0
      || strncmp(t, "RMB", 3) == 0
      || strncmp(t, "XTE", 3) == 0
      || strncmp(t, "BOD", 3) == 0;
}

// Copy the idx-th comma field (0-based; idx 0 = the "$ttSSS" address) of an NMEA
// line into out, stopping at ',', '*', or end. Unlike strtok this preserves empty
// fields, so positional offsets stay correct. Returns false if idx is past the end.
static bool nmea_field(const char* line, int idx, char* out, size_t outsz) {
  const char* p = line;
  for (int cur = 0; cur < idx; cur++) {
    p = strchr(p, ',');
    if (!p) { if (outsz) out[0] = '\0'; return false; }
    p++;
  }
  size_t i = 0;
  while (*p && *p != ',' && *p != '*' && i + 1 < outsz) out[i++] = *p++;
  out[i] = '\0';
  return true;
}

// "ddmm.mmm"/"dddmm.mmm" + hemisphere -> signed decimal degrees.
static double nmea_coord_to_deg(const char* val, char hemi) {
  double raw = atof(val);
  int deg = (int)(raw / 100.0);
  double minutes = raw - deg * 100.0;
  double result = deg + minutes / 60.0;
  if (hemi == 'S' || hemi == 'W') result = -result;
  return result;
}

// RMB fields: 1=status, 6=dest lat, 7=N/S, 8=dest lon, 9=E/W, 13=arrival.
static void garmin_parse_rmb(const char* line) {
  char status = 0, arrival = 0;
  double lat = 0, lon = 0;
  bool have_pos = false;
  char f[20], latv[16], lonv[16], ns[4], ew[4];

  if (nmea_field(line, 1, f, sizeof(f)) && f[0]) status = f[0];
  if (nmea_field(line, 6, latv, sizeof(latv)) && latv[0] &&
      nmea_field(line, 7, ns, sizeof(ns)) && ns[0] &&
      nmea_field(line, 8, lonv, sizeof(lonv)) && lonv[0] &&
      nmea_field(line, 9, ew, sizeof(ew)) && ew[0]) {
    lat = nmea_coord_to_deg(latv, ns[0]);
    lon = nmea_coord_to_deg(lonv, ew[0]);
    have_pos = true;
  }
  if (nmea_field(line, 13, f, sizeof(f)) && f[0]) arrival = f[0];

  navsource_garmin_rmb(status, lat, lon, have_pos, arrival);
}

// Act on one complete line from a Garmin port. `relay` is true only for the live
// channel (COM1/port A): a valid, in-filter line is broadcast as ~APRX, and its
// nav sentences (RMB now; XTE/BOD in step 8) feed the nav source (§2.4).
// Returns a status (also used by the telnet inject hook, garmin_inject_line):
//   0 = relayed as ~APRX (or would be, on a non-relay port)
//   1 = bad/missing checksum (dropped)
//   2 = valid checksum but not a relayed type (filtered, e.g. RMC/GGA)
static int garmin_dispatch_line(const char* line, bool relay, const char* tag) {
#if DEBUG_GARMIN
  DEBUG_PRINT("Garmin ");
  DEBUG_PRINT(tag);
  DEBUG_PRINT(": ");
  DEBUG_PRINTLN(line);
#endif
  if (!nmea_checksum_ok(line)) return 1;       // ignore corrupt lines
  if (!garmin_should_relay(line)) return 2;    // dropped high-rate / unknown type
  if (relay) {
    publish_APRX(line);
    // Feed the live channel's nav sentences into the nav source (§2.4). `relay`
    // also gates this so port B / non-live lines don't drive steering.
    if (strncmp(line + 3, "RMB", 3) == 0) {
      garmin_parse_rmb(line);
    }
  }
  return 0;
}

// Tier-1 test hook (plan §1b / §2.7): inject a complete NMEA line as if
// check_garmin() had assembled it from COM1, exercising checksum + relay with no
// Garmin attached. Returns garmin_dispatch_line's status code.
int garmin_inject_line(const char* line) {
  return garmin_dispatch_line(line, true, "g");
}

// Drain available bytes from one port into its persistent buffer, dispatching
// each complete (CR/LF-terminated) line. Multiple lines may complete per call;
// the GARMIN_MAX_READ_MILLIS cap keeps us from starving sibling tasks.
//
// State is passed as primitive pointers (not GarminPort*) on purpose: arduino-cli
// hoists a generated prototype for this function to the top of the combined
// sketch, before GarminPort is declared, so a custom-type parameter there fails
// to compile. Built-in types in the signature sidestep that.
static void garmin_read_port(HardwareSerial& port, char* buf, int* len, bool* overflow, bool relay, const char* tag) {
  uint32_t read_start_millis = millis();
  while ((millis() - read_start_millis) <= GARMIN_MAX_READ_MILLIS) {
    if (!port.available()) break;
    char c = port.read();
    if (c == '\r' || c == '\n') {
      if (*overflow) {
        *overflow = false;                 // resync at the line boundary
        *len = 0;
      } else if (*len > 0) {
        buf[*len] = '\0';
        garmin_dispatch_line(buf, relay, tag);
        *len = 0;
      }
      continue;
    }
    if (c == '\0') continue;               // skip Nulls
    if (*overflow) continue;               // discarding an over-length line
    if (*len >= GARMIN_MAX_CHARS_PER_LINE) {
      *overflow = true;                    // too long; drop until next CR/LF
      continue;
    }
    buf[(*len)++] = c;
  }
}

// Enqueue one NMEA line to be written to the Garmin UART. Called from the
// AsyncUDP task (~APTX intake). The line is sent verbatim plus CRLF by the
// drain below. Drops silently if the FIFO is full.
void garmin_write_line(const char* nmea) {
  if (!nmea || garmin_tx_queue == NULL) return;
  GarminTxItem item;
  strncpy(item.line, nmea, GARMIN_TX_LINE_MAX - 1);
  item.line[GARMIN_TX_LINE_MAX - 1] = '\0';
  xQueueSend(garmin_tx_queue, &item, 0);   // non-blocking; drop if full
}

// Write up to GARMIN_TX_DRAIN_PER_CALL queued lines to the live UART, each
// terminated with CRLF as the Garmin expects. Runs on command_task.
static void garmin_drain_outbound() {
  if (garmin_tx_queue == NULL) return;
  GarminTxItem item;
  for (int i = 0; i < GARMIN_TX_DRAIN_PER_CALL; i++) {
    if (xQueueReceive(garmin_tx_queue, &item, 0) != pdTRUE) break;
    Serial1Port.print(item.line);
    Serial1Port.print("\r\n");
#if DEBUG_GARMIN
    DEBUG_PRINT("Garmin TX: ");
    DEBUG_PRINTLN(item.line);
#endif
  }
}

void check_garmin() {
  garmin_read_port(Serial1Port, gpA.buf, &gpA.len, &gpA.overflow, true,  "A");   // COM1: live channel, relayed
  garmin_read_port(Serial2Port, gpB.buf, &gpB.len, &gpB.overflow, false, "B");   // COM2: reserved (debug only)
  garmin_drain_outbound();
}
