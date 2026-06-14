---
name: autopilot
description: >-
  Project context for the AutoPilot repo — a DIY marine autopilot for a
  wheel-steered sailboat. Use this skill WHENEVER working anywhere in this
  repository: the Arduino firmware (the `controller` and `display` sketches), the
  UDP telemetry/command protocol between them, the OrangePi navigation computer,
  the PID tuning scripts, or the monitor tool. Trigger it for any task that
  mentions the autopilot, the controller, the display/head unit, the compass/IMU,
  GPS, Garmin/NMEA input, the steering motor, the `~APDAT`/`~APCMD` UDP messages,
  the SoberPilot Wi-Fi network, building/uploading either sketch, or the shared
  `AutoPilot` state class — even if the user doesn't spell out the architecture.
  Read it before editing firmware so you don't re-derive how the two boards talk
  or accidentally break their deliberately different behavior.
---

# AutoPilot project

A DIY autopilot that steers a wheel-driven sailboat. It holds either a **compass
heading** or **navigates to a GPS waypoint**, driving a motor on the wheel. The
system is split across boards that talk over Wi-Fi.

## The big picture: three parts

| Part | Hardware | Role | Location |
|------|----------|------|----------|
| **Navigation computer** | OrangePi Zero 2W, Ubuntu 22.04 + OpenCPN | Plots routes, feeds waypoint/bearing data to the controller as NMEA-0183 | `navigator/` (SD-card image + notes) |
| **Controller** | Arduino Nano ESP32 | The brain: reads sensors, runs the steering logic, drives the motor, is the Wi-Fi access point | `Arduino/controller/` |
| **Display** | Arduino Nano ESP32 (one or more) | Cockpit head unit: shows live state on an LCD, has buttons for control | `Arduino/display/` |

Supporting tooling: `monitor/` (a Python UDP sniffer for debugging the telemetry
stream), `pid/` (offline PID tuning experiments in Python/matplotlib), `circuit/`
(KiCad/hardware), `assets/` (images used in docs).

**The Arduino firmware is the heart of the project and the usual subject of
work.** For build/library/setup details start with `Arduino/README.md` — it is
authoritative and kept current; don't duplicate it, read it.

## How the two boards talk (the protocol)

The controller runs a **Wi-Fi SoftAP** (SSID `SoberPilot`, subnet `10.20.1.x`).
Each display joins it as a station. Communication is plain-text UDP datagrams
framed with a leading `~` and trailing `$`:

- **Telemetry** — controller → display(s), **broadcast on UDP 8888**:
  `~APDAT,<year>,<month>,<day>,<hour>,<minute>,<fix>,<fixquality>,<satellites>,<nav_enabled>,<mode>,<waypoint_set>,<wp_lat>,<wp_lon>,<heading_desired>,<heading>,<pitch>,<roll>,<stability>,<bearing>,<bearing_correction>,<speed>,<distance>,<course>,<location_lat>,<location_lon>$`
  (built in `controller/publish.ino`, parsed in `display/AutoPilot.cpp::parseAPDAT`).
- **Commands** — display → controller, **unicast on UDP 8889**:
  `~APCMD,<cmd>$` (mode changes, heading nudges, tack, etc.).
- **Reset** — `~RESET,1$`.

Because telemetry is broadcast, multiple displays can listen at once; commands
are unicast back to the controller. `mode`: `0`=off, `1`=compass-hold,
`2`=waypoint navigate.

### Optimistic UI

When a button is pressed the display updates its **own** local state immediately
(snappy feedback) and *then* sends the command. To stop the next incoming
telemetry broadcast from clobbering that local change before the controller has
acted on it, the display suppresses the operator-controlled fields for
`LOCAL_COMMAND_SUPPRESS_MS` after a press (see `localCommandTime` in
`display/AutoPilot.cpp`). Keep this in mind when touching either the parser or the
button code.

## Firmware file map

**`controller/`** (Wi-Fi AP, broadcasts telemetry on 8888, listens for commands on 8889):
`controller.ino` (setup/loop, FreeRTOS tasks) · `compass.ino` (BNO08x IMU) ·
`gps.ino` (Adafruit GPS NMEA) · `garmin.ino` (Garmin NMEA-0183 in) ·
`pid.ino` (heading-error → steering correction) · `motor.ino` (steering motor) ·
`publish.ino` (`~APDAT` out) · `subscribe.ino` (`~APCMD` in) · `telnet.ino` ·
`wifi.ino` (SoftAP) · `AutoPilot.{h,cpp}` (state model).

**`display/`** (Wi-Fi station, listens on 8888, sends commands on 8889):
`display.ino` · `screen.ino` (GFX + HX8357 LCD) · `button.ino` (input + optimistic
update) · `command.ino` (`~APCMD` out) · `subscribe.ino` (`~APDAT` in) ·
`volt_meter.ino` (battery/input voltage) · `wifi.ino` (joins SoberPilot) ·
`AutoPilot.{h,cpp}` (local mirror + parser).

## The `AutoPilot` class — read this before "deduplicating" it

Both sketches have an `AutoPilot.{h,cpp}` that looks nearly identical (same field
names, same mutex-guarded getter pattern). **They are not safe to merge into one
shared file**, and this has already been investigated — don't redo that analysis
from scratch or naively collapse them. The shared *shape* hides genuinely
different, role-specific behavior:

- The **controller** is the authority: its setters compute navigation
  (`setFix` has the GPS-loss → compass-hold fallback, `setMode` returns `int` and
  handles `compass_fallback`, `setLoation`/`setWaypoint` recompute bearing &
  distance, plus motor/steering and geo-math helpers).
- The **display** is an optimistic mirror: it has the `~APDAT/~APCMD` parser,
  battery/input voltage averaging, tack request, `connected`/`reset`, the
  `localCommandTime` suppression, and broken-out `year/month/...` fields. Its
  `setMode` returns `void`; note the controller's accessor is misspelled
  `isNavigationEn**d**abled()` while the display's is `isNavigationEnabled()`.

What is truly common is only the boilerplate (recursive-mutex `lock`/`unlock`,
`normalizeDegrees`, `getCourseCorrection`, and the plain locked getters). If
sharing is ever desired, the only safe shape is a **shared base class**
(`AutoPilotState` with the common fields/getters) plus a per-sketch subclass for
the divergent logic — never a single flat superset, which would silently change
one board's behavior.

## Building & uploading

Full instructions live in `Arduino/README.md`. The short version: each sketch has
a `sketch.yaml` defining a `nano` profile (board `arduino:esp32:nano_nora` + pinned
libraries), so `arduino-cli` installs everything itself — there is intentionally
**no** `Arduino/libraries/` folder.

```bash
cd Arduino/controller        # or Arduino/display
arduino-cli compile --profile nano
arduino-cli upload  --profile nano -p /dev/cu.usbmodemXXXX   # see `arduino-cli board list`
```

Gotchas worth remembering:
- Each sketch needs an `arduino_secrets.h` (copy the `.example`); the Wi-Fi
  password **must match** on controller and every display.
- Libraries are declared only in the two `sketch.yaml` files and the README table
  — when adding a new `#include`, update those, don't vendor the library.

## Debugging

- `monitor/monitorAutoPilot.py` — run on any machine on the `SoberPilot` network
  to print the decoded `~APDAT` telemetry stream live. First reach for this when
  diagnosing what the controller is actually sending.
- The controller also exposes a **telnet** console (`controller/telnet.ino`).
