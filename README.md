# AutoPilot Project

A DIY autopilot that steers a wheel-driven sailboat.  It holds either a
**compass heading** or **navigates to a GPS waypoint**, driving a motor on the
steering wheel. Two standalone sensors — rudder angle and masthead wind — report
to it over the same Wi-Fi network.

## System overview

Every unit joins one Wi-Fi network, `SoberPilot` (`10.20.1.x`), which the
controller itself hosts:

| Part | Hardware | Role |
|------|----------|------|
| **Controller** | Arduino Nano ESP32 | The brain: reads IMU + GPS, runs PID steering, drives the motor, hosts the `SoberPilot` access point, broadcasts telemetry |
| **Display unit(s)** | Arduino Nano ESP32 + 320×480 TFT | Cockpit head unit: live autopilot state on a colour LCD, five buttons for mode/heading control. Panel is an HX8357 module or a ST7365P, detected at boot |
| **Rudder sensor** | Arduino Nano ESP32 + AS5600L | Standalone board at the quadrant: reports rudder angle, which a wheel-steered boat has no other way to know |
| **Wind sensor** | Arduino Nano ESP32 + AS5600L, Hall switch, DS18B20 | Standalone masthead unit: apparent wind angle and speed, plus air temperature |
| **Navigator** | Raspberry Pi 5 (8 GB) + OpenCPN | Chart plotter: GPS, AIS and vector charts; the host for the plugin below. See [`navigator/README.md`](navigator/README.md) for the hardware alternatives that were evaluated |
| **OpenCPN plugin** | `autopilot_pi` (C++/wxWidgets) | Software display unit inside OpenCPN: mirrors the TFT panel on screen, sends commands, pushes active waypoints to the controller |

![How the units talk to each other](assets/circuit/system-overview.svg)

The controller is the only unit anything else talks to — the sensors, the
display and the navigator never talk to each other.

## The protocol

All communication uses plain-text UDP datagrams framed `~…$`, on four pairs of
ports:

- **Telemetry** `~APDAT,…$` — controller → everyone, **broadcast UDP 8888**, ~1 Hz.
  31 fields: date/time, GPS fix, nav_enabled, mode, waypoint, heading, pitch,
  roll, stability, bearing, speed, distance, course, location, and the rudder
  angle with its health flag. `controller/publish.ino` is the wire format;
  `AutoPilot::parse` is the reader.
- **Commands** `~APCMD,<cmd>$` — display or navigator → controller, **unicast
  UDP 8889**: `m1`/`m2` (mode), `n0`/`n1` (nav enable), `a±N.NN` (heading
  adjust), `w<lat>,<lon>` (set waypoint), `X` (follow stopped), `t0`/`t1`/`t2`
  (PID auto-tune abort/arm/start), `z` (centre the rudder — relayed on to the
  rudder board).
- **Rudder sensor** — `~APRUD,<angle>,<magnet_ok>$` sensor → controller on
  **UDP 8890**; `~APCMD,z$` back on **UDP 8891**.
- **Wind sensor** — `~APWND,<direction>,<speed_kn>,…$` sensor → controller on
  **UDP 8892**; `~APCMD,v$` (vane zero) and `~APCMD,k<slope>,<offset>$`
  (speed calibration) back on **UDP 8893**.
- **Reset** `~RESET,1$`.

`mode`: `0` = off, `1` = compass-hold, `2` = waypoint-navigate.

Telemetry is broadcast, so any number of displays and plugins can listen at
once; everything else is unicast. Both sensor calibrations are runtime commands
persisted in NVS, so a unit that is already up a mast never has to be reflashed.
[`firmware/Arduino/README.md`](firmware/Arduino/README.md) is the authoritative protocol
reference.

## Directories

```
firmware/               Everything that runs on a microcontroller, and its test tooling
  Arduino/
    controller/         Firmware — the autopilot brain
    display/            Firmware — TFT head unit
    rudder/             Firmware — rudder angle sensor
    wind/               Firmware — masthead wind sensor
    garmin/             Standalone sketch for bringing up the Garmin NMEA input
    libraries/          Vendored Arduino libraries
    scripts/            Serial-link and upload helpers (arduino_link.py, arduino_upload.py)
    README.md           Pinouts, build + library instructions (authoritative)
  emulator/             Garmin GPSMAP 276c emulator + the NMEA wire-contract test harness
  experiments/
    pid/                Offline PID tuning scripts (Python/matplotlib)
navigator/              Everything that runs on the Raspberry Pi 5
  README.md             Pi 5 setup: Ubuntu, Wi-Fi, OpenCPN Flatpak, NVMe boot
  boot/ etc/ home/      Config files as deployed on the Pi (udev, systemd, XFCE)
  opencpn_plugin/
    autopilot_pi/       OpenCPN plugin source + Flatpak build, with its own README
circuit/                EasyEDA exports — seven PCBs, one directory each, README per board
cad/                    Enclosures — one directory per unit (only the wind sensor is built)
assets/                 Images and diagrams used in documentation
```

## Quick-start by component

**Firmware** — see [`firmware/Arduino/README.md`](firmware/Arduino/README.md) for pinouts,
libraries, build, and upload commands. All four sketches (controller, display,
rudder, wind) need a matching `arduino_secrets.h`.

**Navigator** — see [`navigator/README.md`](navigator/README.md) for the
Raspberry Pi 5 build: Ubuntu 24.04, NVMe boot, Wi-Fi onto SoberPilot, and the
OpenCPN Flatpak install.

**OpenCPN plugin** — see
[`navigator/opencpn_plugin/autopilot_pi/README.md`](navigator/opencpn_plugin/autopilot_pi/README.md)
for the panel layout, build command, and send-waypoint workflow.

**Boards** — see [`circuit/README.md`](circuit/README.md) for the seven PCBs,
how they stack into the four units, and the Gerbers to send to a board house.

**Enclosures** — see [`cad/`](cad/). The [wind sensor](cad/wind/README.md) is
built and printable; the other three are placeholders.

**Testing without a boat** — [`firmware/emulator/`](firmware/emulator/README.md) stands in for the
Garmin GPSMAP 276c on the controller's NMEA UART, and carries the test harness
for the wire contract.
