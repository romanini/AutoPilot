---
name: autopilot
description: >-
  Project context for the AutoPilot repo — a DIY marine autopilot for a
  wheel-steered sailboat. Use this skill WHENEVER working anywhere in this
  repository: the Arduino firmware (the `controller` and `display` sketches), the
  UDP telemetry/command protocol between them, the OrangePi navigation computer,
  the OpenCPN plugin (`autopilot_pi`), the PID tuning scripts, or the monitor
  tool. Trigger it for any task that mentions the autopilot, the controller, the
  display/head unit, the compass/IMU, GPS, Garmin/NMEA input, the steering motor,
  the `~APDAT`/`~APCMD` UDP messages, the SoberPilot Wi-Fi network, building or
  uploading either sketch, the shared `AutoPilot` state class, the OpenCPN plugin,
  `autopilot_pi`, `AutoPilotLink`, `AutoPilotPanel`, or Flatpak — even if the user
  doesn't spell out the architecture. Read it before editing firmware or plugin
  code so you don't re-derive how the components talk or accidentally break their
  deliberately different behavior.
---

# AutoPilot project

A DIY autopilot that steers a wheel-driven sailboat. It holds either a **compass
heading** or **navigates to a GPS waypoint**, driving a motor on the wheel. The
system is split across boards that talk over Wi-Fi.

## The big picture: four parts

| Part | Hardware | Role | Location |
|------|----------|------|----------|
| **Controller** | Arduino Nano ESP32 | The brain: reads IMU + GPS, runs PID steering, drives the motor, is the Wi-Fi access point | `Arduino/controller/` |
| **Display** | Arduino Nano ESP32 + HX8357 TFT (one or more) | Cockpit head unit: shows live state on a colour LCD, has physical buttons | `Arduino/display/` |
| **Navigation computer** | OrangePi Zero 2W, Ubuntu 22.04 + OpenCPN | Chart plotter: GPS + AIS + vector charts; also runs `autopilot_pi` | `navigator/` |
| **OpenCPN plugin** | `autopilot_pi` C++/wxWidgets Flatpak extension | Software display unit inside OpenCPN — mirrors TFT layout, sends commands, pushes active waypoints to controller | `opencpn_plugin/autopilot_pi/` |

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

## The navigation computer (OrangePi Zero 2W)

Full details and setup commands are in `navigator/README.md` — read it before
touching networking, OpenCPN, or anything system-level on this box. Summary:

- **Two Wi-Fi interfaces**: onboard `wlan0` joins the controller's `SoberPilot`
  AP (`10.20.1.x`, route metric 600); a USB Wi-Fi adapter joins the
  home/internet network (`172.16.0.x`, route metric 100). Lower metric wins, so
  internet traffic goes out the USB adapter while `10.20.1.0/24` traffic
  (talking to the controller) stays on `wlan0`.
- **`wlan0` keep-alive**: without help, `wlan0` powersaves and misses the
  controller's broadcast `~APDAT` telemetry on UDP 8888. Fixed by
  `/etc/udev/rules.d/10-wifi-disable-powermanagement.rules` (turns off Wi-Fi
  power management on `wlan0`) plus `wifi-keepalive.service` (continuously
  pings `10.20.1.1`, the controller's AP gateway).
- **OpenCPN** is installed as a **Flatpak** (`org.opencpn.OpenCPN`, user
  install, from Flathub), with `devices=all` override so it can reach serial
  ports from inside the sandbox. Only plugin installed: **o-charts_pi**
  (encrypted vector charts).
- **Serial NMEA inputs to OpenCPN**: `/dev/ttyUSB0` @ 4800 baud is a
  GlobalSat BU-353-N5 USB GPS receiver; `/dev/ttyACM0` @ 38400 baud is a dAISy
  AIS receiver. `/etc/udev/rules.d/70-serial-opencpn.rules` sets these to
  `MODE="0666"` so the sandboxed app can open them.
- System updates: the GUI "Software Updater" can silently fail to commit
  (no PolicyKit auth agent in this session — simulate works, the real install
  doesn't, and the dialog just closes). Use `sudo apt update && sudo apt
  full-upgrade` from a terminal instead.

## The OpenCPN plugin (`autopilot_pi`)

Source lives at `opencpn_plugin/autopilot_pi/`.  Full details in
`opencpn_plugin/autopilot_pi/README.md` — read it before touching plugin code.

### What it is

A C++/wxWidgets OpenCPN plugin (API v1.17, `opencpn_plugin_117`) that acts as a
**software display unit**: it joins the SoberPilot Wi-Fi as the fourth client,
receives the same `~APDAT` broadcast the physical TFT display units get, renders
a matching panel on screen, and sends `~APCMD` commands to the controller.

### Architecture

Three classes:

- **`AutoPilotPlugin`** — OpenCPN entry point (`create_pi`).  Owns the
  `wxAuiManager` floating pane, toolbar "AP" button, and handles
  `SetActiveLegInfo` (resolves active waypoint lat/lon via
  `GetActiveWaypointGUID()` + `GetSingleWaypoint()` and passes it to the panel).

- **`AutoPilotLink`** — UDP layer.  Two `wxDatagramSocket`s (receive on
  `0.0.0.0:8888`, send to `10.20.1.1:8889`).  A 250 ms `wxTimer` drains
  incoming packets.  Parses `~APDAT` into `AutoPilotState`.  **Optimistic
  state**: `SendMode`/`SendNavEnable`/`SendAdjust` update `m_state` locally
  and call `UpdateFromState` immediately, then suppress those fields in the
  next 2 s of incoming telemetry (mirrors `localCommandTime` in the display
  Arduino sketch).  Connection times out after 10 s with no packet.

- **`AutoPilotPanel`** (`wxScrolledWindow`) — fixed-pixel 3-column layout
  built in `BuildUI()` using `MakeBox()` (coloured border → black interior →
  coloured title pill).  Virtual size set via `FitInside()` so the AUI pane is
  sized exactly to content.  `UpdateFromState()` refreshes labels and button
  enable/disable states at every telemetry tick.

### Panel layout (480 px wide)

```
LEFT (160×198px)  │  MID (160×160px)  │  RIGHT (160×160px)
Speed [CYAN]      │  Destination      │  Distance [CYAN]
Heading [YELLOW]  │    [LAVENDER]     │  Course   [GREEN]
Pitch   [YELLOW]  │  Bearing [ORANGE] │  Location [GREEN]
Roll    [YELLOW]  ├───────────────────┴──────────────────────
Stability[YELLOW] │  Date/Time [WHITE, 213px] │ Send WP btn
──────────────────┴──────────────────────────────────────────
[sep]  Mode  << 10  < 1  1 >  10 >>  Enable/Disable
```

Left column total height (198 px) = mid+right data (160 px) + date bar (38 px)
so all column tops and bottoms are flush.

### Controls

Single button row: **Mode · << 10 · < 1 · 1 > · 10 >> · Enable/Disable**.
All disabled when no link.  Mode + adjust buttons disabled when nav is off.
Mode toggles 1 ↔ 2 (goes to 2 only if `waypoint_set`; otherwise stays at 1).
Adjust buttons auto-switch controller from mode 2 → 1 before applying delta.

**Send WP** button (embedded in data area, bottom-right): enabled only when
connected AND OpenCPN has an active route leg.  Sends `~APCMD,w<lat>,<lon>$`
without changing mode — user controls mode separately.

### Build

```bash
cd opencpn_plugin/autopilot_pi
flatpak-builder --user --install --force-clean \
    build-dir flatpak/org.opencpn.OpenCPN.Plugin.autopilot.yaml
```

SDK: `org.freedesktop.Sdk//25.08`.  Plugin installs to
`/app/extensions/autopilot/lib/opencpn/libautopilot_pi.so`.

**After any crash** remove the load stamp or OpenCPN will refuse to load the plugin:
```bash
rm ~/.var/app/org.opencpn.OpenCPN/config/opencpn/load_stamps/libautopilot_pi
```

### Key gotchas

- The AUI pane must stay **floating** — docking breaks the layout.  `OnToolbarToolCallback`
  force-floats it whenever shown.  If it gets docked anyway, remove the AutoPilot
  entry from `AUIPerspective` in `~/.var/app/org.opencpn.OpenCPN/config/opencpn/opencpn.conf`.
- `FitInside()` sets virtual size from the sizer after `BuildUI()`.  The AUI
  pane reads this back via `m_panel->GetVirtualSize()` — so pane sizing is
  automatic and doesn't require updating a hardcoded constant when layout changes.
- The `AutoPilotState` struct in `AutoPilotLink.h` mirrors `~APDAT` field order
  exactly.  If the controller adds fields to `publish.ino`, update `ParsePacket()`
  and the struct together.
