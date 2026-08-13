# Controller board — `SM AP Controller`, rev 2.1

The main board of the autopilot. It carries the Arduino Nano ESP32 and all of
its sensors, and breaks out the steering-motor and power connections.

![Controller schematic](SCH_Controller_PNG.png)

![Controller PCB](PCB_Controller_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| **U1** | **Arduino Nano ESP32** | The brain — runs the controller firmware, the PID steering loop, and the `SoberPilot` Wi-Fi access point |
| **U6** | **Adafruit BNO085** (BNO08x 9-DOF IMU) | Fused compass / heading, pitch and roll, plus motion "stability" — connected over I²C (SCL/SDA) |
| **U4 / V3** | **Adafruit GPS Breakout** | GPS position, speed and course, NMEA over the ESP32 UART (TX/RX) |
| **U3** | **Adafruit MPM3610 5 V module** | 12 V → 5 V buck regulator that powers the board. Its `ENABLE` pin is an explicit no-connect — it relies on the pull-up to `VIN` on Adafruit's breakout |
| **U2** | **CD4010BE** hex buffer (TI) | "Receive Buffer" — level-shifts/buffers the incoming serial line |
| **U5** | **CD4010BE** hex buffer (TI) | "Transmit Buffer" — level-shifts/buffers the outgoing serial line |
| **R1** | 47 Ω | Current limit for the on-board **Debug LED** |
| **R2** | 5.1 kΩ | USB-C `CC` pull-down on the Arduino USB connector |
| — | HDR-M 2×4 | **Motor Control** header (out to the steering-motor driver) |
| — | JST-XH 2-pin | **Kill Switch** input |
| — | HDR-M 2×10 | **Power Distribution** header (12 V in / interconnect to the 12-volt-power board) |
| — | JST-XH 1×3/1×4 | Sensor + USB break-out connectors |

The two **CD4010 hex buffers** sit between the ESP32's 3.3 V logic and the
external 5 V / NMEA serial world, buffering the receive and transmit lines so the
Garmin / GPS serial signals are cleanly level-matched before they reach the
microcontroller.

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. It shares a case with the
[12-volt power board](../Controller-12-volt-power/README.md). Firmware:
[`../../Arduino/README.md`](../../Arduino/README.md). Enclosure: [`../../cad/controller/`](../../cad/controller/README.md).
