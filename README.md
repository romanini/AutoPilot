# AutoPilot Project

A DIY autopilot that steers a wheel-driven sailboat.  It holds either a
**compass heading** or **navigates to a GPS waypoint**, driving a motor on the
steering wheel.

## System overview

The system has four parts that all talk over the same Wi-Fi network:

| Part | Hardware | Role |
|------|----------|------|
| **Controller** | Arduino Nano ESP32 | The brain: reads IMU + GPS, runs PID steering, drives the motor, runs the Wi-Fi access point (`SoberPilot`), broadcasts telemetry |
| **Display unit(s)** | Arduino Nano ESP32 + HX8357 TFT | Cockpit head unit: shows live autopilot state on a colour LCD, physical buttons for mode/heading control |
| **Navigation computer** | OrangePi Zero 2W + OpenCPN | Chart plotter: provides GPS, AIS, and vector charts; runs the `autopilot_pi` OpenCPN plugin |
| **OpenCPN plugin** | `autopilot_pi` (C++/wxWidgets) | Software display unit inside OpenCPN: mirrors the TFT panel on screen, sends commands, pushes active waypoints to the controller |

```
                    SoberPilot Wi-Fi  10.20.1.x
                              │
          ┌───────────────────┼───────────────────────┐
          │                   │                       │
   Controller (AP)      Display unit(s)        OrangePi / autopilot_pi
   10.20.1.1            10.20.1.x              10.20.1.x
   broadcasts ~APDAT    receive + buttons       receive + buttons
   UDP 8888             send ~APCMD             send ~APCMD
                        UDP 8889                UDP 8889
```

## The protocol

All communication uses plain-text UDP datagrams framed `~…$`:

- **Telemetry** `~APDAT,…$` — controller → all displays, **broadcast UDP 8888**, ~1 Hz.
  25 fields: date/time, GPS fix, nav_enabled, mode, waypoint, heading, pitch,
  roll, stability, bearing, speed, distance, course, location.
- **Commands** `~APCMD,<cmd>$` — any display → controller, **unicast UDP 8889**.
  Commands: `m1`/`m2` (mode), `n0`/`n1` (nav enable), `a±N.NN` (heading adjust),
  `w<lat>,<lon>` (set waypoint).
- **Reset** `~RESET,1$`.

`mode`: `0` = off, `1` = compass-hold, `2` = waypoint-navigate.

## Directories

```
Arduino/
  controller/    Arduino firmware — the autopilot brain
  display/       Arduino firmware — TFT head unit
  README.md      Build + library instructions (authoritative)
navigator/
  README.md      OrangePi setup: Wi-Fi, OpenCPN Flatpak, serial NMEA
opencpn_plugin/
  autopilot_pi/  OpenCPN plugin source + Flatpak build
  README.md      Plugin architecture, build, and usage
monitor/
  monitorAutoPilot.py   UDP sniffer — decode live ~APDAT stream for debugging
pid/             Offline PID tuning scripts (Python/matplotlib)
circuit/         KiCad schematics and PCB layout
assets/          Images used in documentation
```

## Quick-start by component

**Controller / Display firmware** — see `Arduino/README.md` for pinouts,
libraries, build, and upload commands.

**Navigation computer** — see `navigator/README.md` for OrangePi Wi-Fi
configuration, OpenCPN Flatpak install, and serial NMEA setup.

**OpenCPN plugin** — see `opencpn_plugin/autopilot_pi/README.md` for the
panel layout, build command, and send-waypoint workflow.

**Debugging** — run `monitor/monitorAutoPilot.py` on any machine on the
SoberPilot network to watch the live decoded `~APDAT` stream.
