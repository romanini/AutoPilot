# Wind sensor — `SM AP Wind Sensor`, rev 1.0

The masthead board. Same idea as the rudder sensor — a standalone Nano ESP32
joining SoberPilot and unicasting to the controller (UDP 8892/8893) — but with
three sensors on it: apparent wind angle, wind speed, and air temperature. The
long tapered outline is shaped to fit the Yachta sensor head, which is the
enclosure for this unit (see [`../../cad/wind/`](../../cad/wind/)); the only
wiring up the mast is 12 V from the masthead light circuit.

![Wind sensor schematic](SCH_Sensor-Wind_PNG.png)

![Wind sensor PCB](PCB_Sensor-Wind_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| **U1** | **Arduino Nano ESP32** | Runs the `wind/` sketch; joins **SoberPilot** as a station |
| **DIRECTION** | **AMS AS5600L-ASOT** (SOIC-8) | Wind **vane** angle — magnetic encoder on the vane spindle, on `SDA`/`SCL` |
| **Q3** | **Honeywell SS40AF** Hall-effect switch (A3144E footprint) | Wind **speed** — one pulse per cup-wheel magnet pass, open-collector output to `D2` |
| **TEMPERATURE** | **DS18B20** (TO-92) | Air temperature on 1-Wire |
| **R1 / R2** | 4.7 kΩ | I²C pull-ups on `SDA` / `SCL` to **3V3** |
| **R3** | 10 kΩ | Pull-up on the Hall sensor's open-collector output to **3V3** |
| **R4** | 4.7 kΩ | 1-Wire pull-up on the DS18B20 data line to **3V3** |
| **C1** | 100 nF | AS5600L decoupling |
| **J2** | 2-pin 3.5 mm terminal block | **12 V** in (masthead light circuit), to the Nano's `VIN` |

As on the rudder board, the AS5600L runs off **3V3** with `DIR` to GND. The
Hall switch is the one part fed from **12 V** (SS40AF is rated 3.8–30 V); its
open-collector output is pulled up to 3V3, so the ESP32 still only ever sees
3.3 V logic.

Pin assignments match the `wind/` sketch: cup speed on `D2`
(`WIND_SPEED_PIN`), DS18B20 on `D4` (`ONE_WIRE_PIN`), vane on the dedicated
I²C pins.

Both the vane zero and the wind-speed calibration are runtime commands
persisted in NVS, so a unit that is already up the mast never has to be
reflashed to be calibrated — see [`../../firmware/Arduino/README.md`](../../firmware/Arduino/README.md).

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. This board is a sealed one-board
unit on its own. Firmware: [`../../firmware/Arduino/README.md`](../../firmware/Arduino/README.md).
Enclosure: [`../../cad/wind/`](../../cad/wind/README.md).
