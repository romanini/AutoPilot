# Firmware

Everything that runs on a microcontroller, plus the tooling used to test it off
the boat. Four Arduino Nano ESP32 units, one sketch each, all talking over the
`SoberPilot` Wi-Fi network — see the [top-level README](../README.md) for the
protocol and how the units fit together.

## What is in here

| Directory | What it is |
|---|---|
| [`Arduino/`](Arduino/README.md) | All the sketches, the vendored libraries and the upload tooling. **Its README is the authoritative build reference** — pinouts, library table, `arduino-cli` commands |
| [`emulator/`](emulator/README.md) | A Python stand-in for the Garmin GPSMAP 276c on the controller's NMEA UART, plus the authoritative implementation of the NMEA wire contract and its tests |
| `experiments/pid/` | Offline PID tuning — `pid_test.py` replays logged runs and plots them with matplotlib |

## The four units

Each sketch is one physical unit. The board it runs on and the case it lives in
are documented alongside it:

| Sketch | Unit | Board | Enclosure |
|---|---|---|---|
| [`Arduino/controller/`](Arduino/controller/) | The brain — IMU, GPS, PID steering, motor drive, and the Wi-Fi access point | [`circuit/Controller/`](../circuit/Controller/README.md) | [`cad/controller/`](../cad/controller/README.md) |
| [`Arduino/display/`](Arduino/display/) | Cockpit head unit — TFT and five buttons | [`circuit/Display/`](../circuit/Display/README.md) | [`cad/display/`](../cad/display/README.md) |
| [`Arduino/rudder/`](Arduino/rudder/) | Rudder angle sensor at the quadrant | [`circuit/Sensor-Rudder/`](../circuit/Sensor-Rudder/README.md) | [`cad/rudder/`](../cad/rudder/README.md) |
| [`Arduino/wind/`](Arduino/wind/) | Masthead wind sensor — angle, speed, air temperature | [`circuit/Sensor-Wind/`](../circuit/Sensor-Wind/README.md) | [`cad/wind/`](../cad/wind/README.md) |

`Arduino/` also holds single-purpose sketches that are not units: `garmin/` for
bringing up the Garmin NMEA input, and `panel_detect/`, `panel_probe/` and
`st7365_bringup/` for identifying and driving the display panel.

## Before you build

Every sketch needs an `arduino_secrets.h` with the Wi-Fi password, and **it must
match across the controller, every display, and both sensors** — a mismatch
looks like a unit that boots fine and never appears on the network. Copy the
example alongside each sketch to get started.

Libraries are declared in each sketch's `sketch.yaml` and in the table in
[`Arduino/README.md`](Arduino/README.md); when adding an `#include`, update
those rather than vendoring the library.

## Testing without a boat

[`emulator/`](emulator/README.md) drives the controller's Garmin UART over a
USB-serial tap, so route and waypoint handling can be exercised at a desk. It is
a serial device rather than an IP endpoint, and it is the reference
implementation of the NMEA framing the controller parses — if the two ever
disagree, the emulator's tests are the tie-breaker.
