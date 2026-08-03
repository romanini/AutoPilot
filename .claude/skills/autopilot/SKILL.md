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
| **Rudder sensor** | Arduino Nano ESP32 + AS5600 (I2C) | Standalone rudder angle sensor (boat is wheel-steered); joins SoberPilot as a station and reports angle to the controller over UDP | `Arduino/rudder/` |
| **Wind sensor** | Arduino Nano ESP32 + AS5600 vane + reed-switch cup anemometer + DS18B20 | Standalone masthead wind sensor (Yachta head); joins SoberPilot as a station and reports apparent wind to the controller over UDP | `Arduino/wind/` |

Supporting tooling: `experiments/pid/` (offline PID tuning experiments in
Python/matplotlib), `circuit/` (KiCad/hardware), `assets/` (images used in
docs). There is no dedicated UDP monitor script — see Debugging below.

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

The rudder sensor (see below) speaks a separate pair of ports (8890/8891) to
the controller — it isn't part of the 8888/8889 display protocol above.

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

**`rudder/`** (Wi-Fi station, own ports — see below): `rudder.ino` (setup +
FreeRTOS tasks) · `angle.ino` (AS5600 read + calibration + the mutex) ·
`publish.ino` (`~APRUD` out, 8890) · `subscribe.ino` (relayed `~APCMD,z$` in,
8891) · `wifi.ino` (joins SoberPilot, auto-reconnect).

**`wind/`** (Wi-Fi station, own ports — see below): `wind.ino` (setup +
FreeRTOS tasks + sample cadence) · `Wind.{h,cpp}` (state model **and** the
ported wind maths) · `vane.ino` (AS5600 read + bow calibration + the mutex) ·
`anemometer.ino` (reed-switch ISR + rotation timing) · `temperature.ino`
(DS18B20) · `publish.ino` (`~APWND` out, 8892) · `subscribe.ino` (relayed
`~APCMD,v$` in, 8893) · `wifi.ino` (joins SoberPilot, auto-reconnect).

## The rudder position sensor (`Arduino/rudder/`)

A standalone Nano ESP32 reading an AS5600 magnetic angle sensor over I2C,
mounted at the rudder stock/quadrant (boat is wheel-steered, so the sensor
lives at the rudder itself, not the wheel — cable slack makes wheel position
an unreliable proxy). It joins the SoberPilot Wi-Fi as a station (same as a
display) and reports rudder angle to the controller over UDP — it does not
talk to displays directly.

**Wiring:** AS5600 module powered from the Nano's **3V3 pin, not 5V/VIN** — the
module's onboard I2C pull-ups tie SDA/SCL to whatever powers it, and the ESP32's
GPIOs are 3.3V-only (not 5V-tolerant). SDA/SCL to the Nano ESP32's dedicated
SDA/SCL pins. DIR pin tied to GND (clockwise-increasing convention).

**Protocol** (separate ports from the 8888/8889 display protocol above):
- **UDP 8890**, rudder → controller, unicast to `10.20.1.1`:
  `~APRUD,<angle_deg>,<magnet_ok>$`. This is the *only* way the controller
  learns the rudder board's IP (there's no static assignment, no discovery
  mechanism) — `controller/rudder.ino`'s listener remembers the source address
  of every packet it receives and uses it as the relay target below. Until at
  least one packet has arrived, there is nothing to relay `~APCMD,z$` to.
- **UDP 8891**, controller → rudder, unicast to that remembered IP:
  `~APCMD,z$` — "center now" (see calibration below), relayed verbatim from
  whatever sent the original `~APCMD,z$` to the controller (telnet, a display,
  or the OpenCPN plugin).

**Why relay through the controller** rather than commanding the rudder board
directly: telnet, every display, and the OpenCPN plugin already only know how
to address the controller (`10.20.1.1`) — none of them know or need to know
the rudder board's IP. Routing the center command through the controller means
only the controller needs to know the rudder board exists; no new client-side
plumbing is needed if a future UI (display menu, OpenCPN button) wants to
trigger it.

**Command verb `z`, not `t...`:** the controller's `dispatch_command()`
(`controller/subscribe.ino`) switches on `buffer[0]` alone — `t` is already
taken by autotune (`t0`/`t1`/`t2`). A two-letter verb like `tc` would hit the
autotune case and silently abort any in-progress autotune. `z` (zero/center) is
a free, bare one-letter action verb, same shape as the existing `X`.

**Calibration:** the sensor can only be zeroed once it's installed (you can't
know the AS5600's raw offset relative to "rudder dead center" beforehand), so
zeroing is a runtime command, not a one-time build step. On `~APCMD,z$` the
board takes a fresh raw reading and computes
`offset = (2048 - raw) mod 4096` (in raw AS5600 counts, not degrees — avoids
float rounding drift), then persists it via `Preferences` (namespace
`"rudder"`, matching the existing pattern in `controller/pid.ino`) so it
survives a reboot. Every subsequent reading reports
`((raw + offset) mod 4096) * 360/4096`, so dead center always reads as exactly
180° — chosen (instead of 0°) so the bow-at-0°/rudder-at-180° convention holds,
and so the 0°/360° register wraparound lands on the far side of the sensor
from center, safely outside the rudder's actual range of motion. There is no
ack packet — same as every other `~APCMD` in this project, the sender confirms
the change by watching the next `~APRUD` value rather than a reply.

**Current status:** both sides are implemented. Rudder board: WiFi join,
`~APRUD` publish (`Arduino/rudder/publish.ino`, 50 Hz — see below), `~APCMD,z$`
listener, offset persistence (`angle.ino`). Controller
(`controller/rudder.ino`): listens on 8890, remembers the rudder board's IP,
stores the angle and the sensor's raw magnet-detected flag via
`AutoPilot::setRudderAngle()` (mutex-protected, same pattern as
`setPitch`/`setRoll`), and `case 'z':` in `dispatch_command()`
(`subscribe.ino`) relays to it. `~APDAT` (`controller/publish.ino`) gained two
trailing fields: `rudder_angle` (`%.2f`) and `isRudderOk()` (`%d` — old parsers
ignore unknown trailing fields, same pattern as the damped-course fields
already appended there).

**`isRudderOk()` is computed, not the raw magnet flag:** `rudder.ino` tracks
`lastRudderReceiveTime` (updated on every `~APRUD` packet, same pattern as
`display/subscribe.ino`'s `lastReceiveTime`) and combines it with the sensor's
own magnet-detected flag: `isRudderOk()` is true only if the rudder board has
been heard from within the last 1s (`RUDDER_RECEIVE_TIMEOUT_MS`) *and* its
last-reported magnet state was good. This is deliberate: publishing the raw
magnet flag alone would leave the controller reporting the sensor's last
value forever if the rudder board is powered off or loses Wi-Fi - the timeout
is what makes a disconnected rudder board actually read as "no data" instead
of a frozen stale reading. The display mirrors this as `AutoPilot::isRudderOk()`
(`rudder_ok` field) - same combined meaning on both sides, not a raw magnet
flag on either.

**Rudder-board threading — don't collapse this back into one task or drop the
mutex.** Two tasks pinned to separate cores, matching the controller/display
split: `sensor_task` (CORE_0, priority 1) reads the AS5600 and publishes
`~APRUD`; `command_task` (CORE_1, priority 2) runs `check_wifi()` and
`check_calibration_request()`. Both of `command_task`'s jobs block for a long
time — an association attempt can sit for `WIFI_ATTEMPT_TIMEOUT_MS`, and a
calibration does an NVS flash write — which is exactly why they don't share a
core with the 50 Hz sampling.

Two non-obvious rules hold this together:

1. **`angleMutex` (`angle.ino`) is mandatory, not defensive.** Both tasks touch
   the AS5600 and `offsetCounts`. A single `getRawAngle()` is *two* Wire
   transactions (register-address write, then data read), so a transaction
   injected between them from the other core leaves the AS5600's internal
   address pointer pointing elsewhere and the read returns a different
   register's contents. Per-transaction locking inside `TwoWire` does not
   prevent this; the lock has to span the pair.
2. **The AsyncUDP callback only sets a flag.** `process_command()`
   (`subscribe.ino`) calls `request_calibration()`, which sets a volatile flag
   that `command_task` consumes. Calibrating inline in the callback would put an
   I²C read and an NVS flash write on the network stack's own task. The flag is
   cleared before the work runs, so a request arriving mid-calibration is
   serviced next tick rather than swallowed, and repeat requests coalesce
   (correct for an idempotent "make this position center").

**Wi-Fi resilience (`rudder/wifi.ino`):** the board is headless at the rudder
stock, so it must never need a power cycle to rejoin. `setup_wifi()` makes a few
bounded attempts and then *falls through* rather than spinning — blocking in
`setup()` until the AP appears would leave it wedged with the command listener
never started (a real case: the whole boat powers up at once and the controller's
AP isn't there yet). `check_wifi()`, polled from `loop()`, then handles both
"never came up" and "came up, then dropped", throttled by
`WIFI_RETRY_INTERVAL_MS`. **Gotcha:** every fresh link must re-call
`setup_subscribe()` — the UDP listening socket doesn't survive the link going
down, so without the rebind the board keeps publishing `~APRUD` happily but
silently stops accepting relayed `~APCMD,z$`. `rudder/subscribe.ino`'s
`setup_subscribe()` therefore does `commandUdp.close()` before `listen()` so it
is safe to call repeatedly (same reason and same shape as
`display/subscribe.ino`). Powersave is disabled (`WiFi.setSleep(false)`) for the
same reason it is on the navigator's `wlan0`.

**Not yet done:** the OpenCPN plugin rudder box + "Center now" button
(`AutoPilotState`/`ParsePacket()` in `opencpn_plugin/autopilot_pi/AutoPilotLink.h`
need the same two fields added to stay in sync with `~APDAT`).

**Publish rate (50 Hz, not the original 1 Hz):** unlike the human-readable
1 Hz `~APDAT` broadcast, rudder angle is meant to eventually feed a real
control loop (a cascaded heading→rudder-angle→motor loop, see
`.claude/docs/FutureUpgrades-WindAndRudder.md`), so it needs to keep pace with
the controller's existing 100 Hz heading PID (`compass.ino`'s `control_task`,
10 ms loop) rather than a display's refresh rate. 50 Hz was chosen as a
practical middle ground — within the same order of magnitude as the 100 Hz
loop it will eventually feed, without assuming Wi-Fi/UDP can sustain the full
100 Hz reliably (untested on the actual boat network).

## The masthead wind sensor (`Arduino/wind/`)

A standalone Nano ESP32 at the masthead running a **port of Norbert Walter's
Windsensor Yachta firmware** (https://github.com/norbert-walter/Windsensor_Yachta)
from the ESP8266. Hardware is the "Yachta" (1.x) variant: **AS5600** vane
encoder on I2C, reed-switch cup anemometer (2 pulses/rev), **DS18B20** air
temperature on 1-Wire. It joins SoberPilot as a station, exactly like the
rudder board, and unicasts to the controller — it does not talk to displays.

**What was dropped from the original, and why it isn't coming back by accident:**
the HTTP server, settings/gauge/JSON pages, OTA updater and the NMEA-0183 TCP
server are all gone. Configuration that lived on the settings page is now
either a `#define` or (for the one thing that genuinely can't be known before
the head is on the mast) a runtime command. A phone-facing interface is planned
over **Bluetooth**, not by restoring the web server.

**Protocol** (a third pair of ports, separate from 8888/8889 and 8890/8891):
- **UDP 8892**, wind → controller, unicast to `10.20.1.1`:
  `~APWND,<direction>,<speed_kn>,<speed_mps>,<bft>,<temp_c>,<vane_ok>,<temp_ok>$`
  at 5 Hz. `direction` is apparent wind angle 0–360° clockwise from the bow.
  As with the rudder board this is also the only way the controller can learn
  this board's IP.
- **UDP 8893**, controller → wind: `~APCMD,v$` — "the vane is pointing dead
  ahead, call this zero". Verb `v` because `dispatch_command()` switches on
  `buffer[0]` alone and `a/m/n/w/X/t/z` are taken; same reasoning that picked
  `z` for the rudder.

**Two health flags, not one:** `vane_ok` (AS5600 magnet detected) and
`temp_ok` (a DS18B20 answered) are separate because the failures are
independent and mean different things — a dead vane costs direction while speed
keeps working; a dead DS18B20 costs nothing that matters. Neither flag covers
"this board stopped transmitting": that is a **receive timeout on the
controller side**, the same distinction (and the same reason) as
`isRudderOk()`.

**Where the port deviates from the original — these are deliberate, don't
"restore" them:**
1. **Rate limiter is per-second, and wraps correctly.** The original clamps the
   angle change between samples to 45° and disables the clamp entirely within
   45° of the bow (its subtraction can't tell a small move across 0/360 from a
   350° jump). This port clamps to 90°/s using the *signed shortest* difference,
   so it scales with the faster calculate cadence and stays armed close-hauled —
   the sector a future wind-vane mode would actually steer in.
2. **A bad vane read holds the last angle** instead of substituting 0°, which
   on the wire is indistinguishable from a real "wind dead ahead". `vane_ok`
   carries the failure instead.
3. **No ESP8266 tick counter.** The original ran a 100 µs hardware timer purely
   to count ticks between reed pulses; `micros()` in the pulse ISR replaces the
   timer, its ISR, the counters and the marker state machine. The ISR is
   **integer-only on purpose** — touching a float from an ESP32 ISR can corrupt
   the FPU context of whatever task was preempted.
4. **Every pulse is sampled**, where the original recorded only every other one.
   Same quantity, twice the samples.

**Two things that look like arbitrary constants but aren't:**
- **`TEMPERATURE_INTERVAL_MS` is 500 to match the original's poll rate**, not
  because 2 Hz air temperature is useful. The `-6.0 °C` self-heating
  compensation ported from the original was calibrated at that rate; polling
  slower would make the constant wrong. That in turn forces **11-bit** DS18B20
  resolution (375 ms conversion), because the default 12-bit takes 750 ms and
  would not finish inside the poll interval.
- **`ANEMOMETER_PERIOD_LIMIT_MS` is enforced in two places** — the ISR clamps
  intervals to it, `Wind::calculate()` then refuses to convert a period that
  reached it. It is defined once in `Wind.h` so the two layers can't drift.
  Genuinely-stopped cups are caught by the separate 3 s zero-wind timeout.

**Threading** is the rudder board's split, for the rudder board's reasons:
`sensor_task` (CORE_0) samples/calculates/publishes; `command_task` (CORE_1)
runs `check_wifi()` and `check_calibration_request()`, both of which block for
a long time (an association attempt, an NVS write). `vaneMutex` is mandatory
for the same two-Wire-transaction reason as `angleMutex`, and the AsyncUDP
callback only sets a flag.

**Pin naming:** this sketch uses `D2`/`D3` rather than bare integers, so it is
correct under either Arduino IDE Pin Numbering setting — unlike the controller,
which is why that README carries a warning about it.

**Not yet done (controller and downstream side):** nothing on the controller
receives `~APWND` yet. To close the loop it needs a `wind.ino` mirroring
`controller/rudder.ino` (listen on 8892, remember the sender's IP, store into
`AutoPilot`, add a `case 'v':` relay in `dispatch_command()`), then wind fields
appended to `~APDAT` in `controller/publish.ino` with the display parser and
`autopilot_pi`'s `AutoPilotState`/`ParsePacket()` updated **together**.

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
- Libraries are declared only in each sketch's `sketch.yaml` and the README table
  — when adding a new `#include`, update those, don't vendor the library.

## Debugging

- No dedicated monitor script exists anymore (`monitor/monitorAutoPilot.py` was
  removed in commit `7709125`, "cleaning up", 2026-06-20). For raw traffic on
  any machine on the `SoberPilot` network, plain `nc -ul 8888` prints the
  broadcast `~APDAT,...$` lines unparsed (plain text, comma-separated) — good
  enough for "is the controller sending anything" but doesn't decode fields.
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
