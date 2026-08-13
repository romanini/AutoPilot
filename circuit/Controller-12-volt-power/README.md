# 12-volt power board — `SM AP Power`, rev 1.0

A passive **power-distribution and interconnect board** that lives next to the
controller. It brings the boat's 12 V supply, the Garmin NMEA feed and a USB port
onto one board, fuses them, and hands them off to the controller through a single
ribbon header.

![12 V power schematic](SCH_Controller-12-volt-power_PNG.png)

![12 V power PCB](PCB_Controller-12-volt-power_PNG.png)

## Components

| Ref | Part | Role |
|-----|------|------|
| **U2** | **DBT50G-9.5-6P** terminal/power connector | Main 12 V power entry block |
| — | **Garmin In** connector | Garmin NMEA-0183 feed — Tx/A (blue), Rx/A (yellow), Tx/B (violet), Rx/B (green), Alarm (white), Ground (black), 12 V (red) |
| — | **USB-C** connector | USB-In break-out (Vin / D− / D+ / GND / CC) |
| — | 5×20 mm fuse holder (BLX-A) | **Acc Fuse** — protects the 12 V accessory rail |
| — | 5×20 mm fuse holder (BLX-A) | **Garmin Fuse** — protects the Garmin 12 V feed |
| **R2** | **0 Ω** link (with test point **TP1**) | The *only* bridge between the ribbon/Garmin/USB ground and the main power-connector ground — a star-ground link that can be lifted to break a loop or measure return current. Marked `0k` on the schematic |
| — | HDR-M 2×10 | **Control Board** header — ribbon to the controller's Power Distribution header |

Everything here is wiring and protection: it consolidates power and the Garmin
NMEA wiring, fuses the 12 V rails, and routes them to the controller over the
2×10 ribbon so the controller board itself stays clean.

---

One of the [AutoPilot circuit boards](../README.md) — that README covers how the
boards fit together and the open review items. It shares a case with the
[controller board](../Controller/README.md). Firmware:
[`../../Arduino/README.md`](../../Arduino/README.md). Enclosure: [`../../cad/controller/`](../../cad/controller/README.md).
