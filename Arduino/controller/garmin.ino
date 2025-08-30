#include <HardwareSerial.h>

#define DEBUG_GARMIN 1

#define GARMIN_TX_A A1
#define GARMIN_RX_A A0
#define GARMIN_TX_B A3
#define GARMIN_RX_B A2

// Create two extra HardwareSerial ports
//   UART_NUM_1  → "Serial1"
//   UART_NUM_2  → "Serial2"
HardwareSerial Serial1Port(1);
HardwareSerial Serial2Port(2);

void setup_garmin() {

  //Serial1 on 9,600 baud noote that the pins are revered because we want the Garmin TX to connect to an RX pin
  // and the Garmin RX to connect to a TX pin.  The  being() function parsm are in order of RX then TX
  Serial1Port.begin(4800, SERIAL_8N1, GARMIN_TX_A, GARMIN_RX_A);

  //Serial2 on 9,600 baud noote that the pins are revered because we want the Garmin TX to connect to an RX pin
  // and the Garmin RX to connect to a TX pin.  The  being() function parsm are in order of RX then TX
  Serial2Port.begin(4800, SERIAL_8N1, GARMIN_TX_B, GARMIN_RX_B);

  Serial.println("Garmin A and B setup");
}

void check_garmin() {
  String line;
  while (true) {
    if (!Serial1Port.available()) break;
    char c = Serial1Port.read();
    if (c == '\n' || c == '\r') break;
    if (c == '\0') continue;  // skip Nulls
    line += c;
  }
#if DEBUG_GARMIN
  if (line.length() > 0) {
    Serial.print("Garmin A:");
    Serial.println(line);
  }
#endif

  line.clear();
  while (true) {
    if (!Serial2Port.available()) break;
    char c = Serial2Port.read();
    if (c == '\n' || c == '\r') break;
    if (c == '\0') continue;  // skip Nulls
    line += c;
  }
#if DEBUG_GARMIN
  if (line.length() > 0) {
    Serial.print("Garmin B: '");
    Serial.print(line);
    Serial.println("'");
  }
#endif
}
