# Rudder sensor — `SM AP Rudder Sensor`, rev 1.0

A standalone board that reports rudder angle to the controller over Wi-Fi
(UDP 8890/8891) — the boat is wheel-steered, so the controller has no other way
to know where the rudder actually is. It is a unit on its own: one board in its
own case down by the quadrant, with nothing running to it but a 12 V pair.
Deliberately minimal — a Nano ESP32, a magnetic angle encoder, and 12 V in.

![Rudder sensor schematic](SCH_Sensor-Rudder_PNG.png)

![Rudder sensor PCB](PCB_Sensor-Rudder_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| **U1** | **Arduino Nano ESP32** | Runs the `rudder/` sketch; joins **SoberPilot** as a station |
| **U2** | **AMS AS5600L-ASOT** (SOIC-8) | Contactless 12-bit magnetic rotary encoder — reads the angle of a magnet on the rudder stock |
| **R1 / R2** | 4.7 kΩ | I²C pull-ups on `SDA` / `SCL` to **3V3** |
| **C1** | 100 nF | AS5600L decoupling |
| **J1** | 2-pin 3.5 mm terminal block | **12 V** in, straight to the Nano's `VIN` |

Both AS5600L supply pins (`VDD5V`, `VDD3V3`) are tied to the Nano's **3V3**
pin, not 5 V — a 5 V-powered module would pull the I²C lines above what the
ESP32's 3.3 V-only GPIOs tolerate. `DIR` is tied to GND, fixing the direction of
increasing counts. The sensor sits on the Nano's dedicated `SDA`/`SCL` pins
(`ADC4/SDA`, `ADC5/SCL`); everything else on the Nano is unused.

Centring is a runtime command (`~APCMD,z$`), not a build constant, so the
zero offset can be set with the rudder physically amidships and is persisted in
NVS — see [`../../firmware/Arduino/README.md`](../../firmware/Arduino/README.md).

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. This board is a sealed one-board
unit on its own. Firmware: [`../../firmware/Arduino/README.md`](../../firmware/Arduino/README.md).
Enclosure: [`../../cad/rudder/`](../../cad/rudder/README.md).
