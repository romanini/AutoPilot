---
name: autopilot
description: >-
  Project context for the AutoPilot repo — a DIY marine autopilot for a
  wheel-steered sailboat. Use this skill WHENEVER working anywhere in this
  repository: the Arduino firmware (the `controller`, `display`, `rudder` and
  `wind` sketches), the UDP telemetry/command protocol between them, the
  Raspberry Pi 5 navigation computer, the OpenCPN plugin (`autopilot_pi`), or
  the PID tuning scripts. Trigger it for any task that mentions the autopilot,
  the controller, the display/head unit, the rudder angle sensor, the masthead
  wind sensor, the compass/IMU, GPS, Garmin/NMEA input, the steering motor, the
  AS5600/AS5600L encoders, the `~APDAT`/`~APCMD`/`~APRUD`/`~APWND` UDP messages,
  the SoberPilot Wi-Fi network, building or uploading any of the sketches, the
  shared `AutoPilot` state class, the OpenCPN plugin, `autopilot_pi`,
  `AutoPilotLink`, `AutoPilotPanel`, or Flatpak — even if the user doesn't spell
  out the architecture. Read it before editing firmware or plugin
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
| **Controller** | Arduino Nano ESP32 | The brain: reads IMU + GPS, runs PID steering, drives the motor, is the Wi-Fi access point | `firmware/Arduino/controller/` |
| **Display** | Arduino Nano ESP32 + HX8357 TFT (one or more) | Cockpit head unit: shows live state on a colour LCD, has physical buttons | `firmware/Arduino/display/` |
| **Navigation computer** | Raspberry Pi 5 (8 GB), Ubuntu 24.04 + OpenCPN | Chart plotter: GPS + AIS + vector charts; also runs `autopilot_pi` | `navigator/` |
| **OpenCPN plugin** | `autopilot_pi` C++/wxWidgets Flatpak extension | Software display unit inside OpenCPN — mirrors TFT layout, sends commands, pushes active waypoints to controller | `navigator/opencpn_plugin/autopilot_pi/` |
| **Rudder sensor** | Arduino Nano ESP32 + AS5600 (I2C) | Standalone rudder angle sensor (boat is wheel-steered); joins SoberPilot as a station and reports angle to the controller over UDP | `firmware/Arduino/rudder/` |
| **Wind sensor** | Arduino Nano ESP32 + AS5600 vane + reed-switch cup anemometer + DS18B20 | Standalone masthead wind sensor (Yachta head); joins SoberPilot as a station and reports apparent wind to the controller over UDP | `firmware/Arduino/wind/` |

Supporting tooling: `firmware/experiments/pid/` (offline PID tuning experiments in
Python/matplotlib), `circuit/` (KiCad/hardware), `cad/` (FreeCAD enclosure sources
and printable STLs — see CAD & enclosures below), `assets/` (images used in
docs). There is no dedicated UDP monitor script — see Debugging below.

**The Arduino firmware is the heart of the project and the usual subject of
work.** For build/library/setup details start with `firmware/Arduino/README.md` — it is
authoritative and kept current; don't duplicate it, read it.

## How the two boards talk (the protocol)

The controller runs a **Wi-Fi SoftAP** (SSID `SoberPilot`, subnet `10.20.1.x`).
Each display joins it as a station. Communication is plain-text UDP datagrams
framed with a leading `~` and trailing `$`:

- **Telemetry** — controller → display(s), **broadcast on UDP 8888**:
  `~APDAT,<year>,<month>,<day>,<hour>,<minute>,<fix>,<fixquality>,<satellites>,<nav_enabled>,<mode>,<waypoint_set>,<wp_lat>,<wp_lon>,<heading_desired>,<heading>,<pitch>,<roll>,<stability>,<bearing>,<bearing_correction>,<speed>,<distance>,<course>,<location_lat>,<location_lon>`
  then the appended trailing fields, in order:
  `,<nav_source>,<autotune_state>,<cog_damped>,<cog_damped_valid>,<rudder_angle>,<rudder_ok>,<awa>,<aws_kn>,<wind_ok>,<twa>,<tws_kn>,<true_wind_ok>,<air_temp_c>,<temp_ok>$`
  — 39 fields (built in `controller/publish.ino`, parsed in
  `display/AutoPilot.cpp::parseAPDAT` and `autopilot_pi`'s `ParseApdat()`).
  **Fields are only ever appended, never reordered or removed**, and every
  receiver treats trailing fields as optional, so a display running older
  firmware keeps working against a newer controller.
- **Commands** — display → controller, **unicast on UDP 8889**:
  `~APCMD,<cmd>$` (mode changes, heading nudges, tack, etc.).
- **Reset** — `~RESET,1$`.

Because telemetry is broadcast, multiple displays can listen at once; commands
are unicast back to the controller. `mode`: `0`=off, `1`=compass-hold,
`2`=waypoint navigate.

The two sensor boards each speak their own pair of ports to the controller and
are not part of the 8888/8889 display protocol above: rudder on 8890/8891, wind
on 8892/8893.

### Two command surfaces, not one — check both

`~APCMD` over UDP (`dispatch_command()`, `controller/subscribe.ino`) and the
**telnet console** (`process_telnet()`, `controller/telnet.ino`) are separate
dispatchers with separate switches and separate verb sets. **Adding a verb to
one does not add it to the other**, and they share no parser. This has bitten
before: `~APCMD,z$` existed on the UDP side for months while telnet had no way
to send it, so "center the rudder" was reachable only from the OpenCPN plugin.

The split is deliberate — telnet is a human interface that can answer back,
where `~APCMD` is fire-and-forget with no ack by design — but anything meant to
be reachable from both has to be wired up twice. Who can send what today:

| | UDP `~APCMD` | Telnet |
|---|---|---|
| `a` `m` `w` | display, plugin | yes |
| `t` (autotune) | display, plugin | `pat` (arm only) |
| `X` | plugin | no |
| `z` `v` `d` `k` (sensor calibration) | plugin sends `z` only | **yes, all four** |
| `n` (navigation on/off) | **retired — ignored** | **retired** — replies with why |

The displays send only `a`, `m`, `t` — they have buttons, not a keyboard, so the
calibration verbs will never come from there.

**`n` is retired on purpose — don't put it back.** Navigation is engaged and
disengaged *only* by the motor-enable switch wired to the controller board (see
"The motor-enable switch" below). `dispatch_command()` has no `n` case at all;
telnet keeps one solely to *answer* ("Navigation is set by the motor-enable
switch on the controller only"), because it can, and because otherwise a typed
`n1` falls through to "Unknown command" and sends the operator hunting for a
typo instead of looking at the switch. That asymmetry is the two-surfaces split
working as intended, not an oversight.

**What telnet can and cannot report about a relayed command.** No `~APCMD` has
an ack, so telnet can never tell you a sensor board *received* a datagram,
*accepted* a value, or wrote it to flash. But two failures are knowable purely
locally, and `report_relay_reachability()` (telnet.ino) reports both:

1. **Never heard from that board** → no address exists, the command is
   definitively going nowhere, so it isn't even sent.
2. **Address known but the board has gone quiet** (`>SENSOR_QUIET_MS`) → the
   datagram still goes out, but the reply says so rather than "ok". This case
   needs `rudder_last_heard_ms()`/`wind_last_heard_ms()` because
   `rudderIpKnown`/`windIpKnown` **never expire once set** — a board that was
   seen once and then lost power would otherwise report a cheerful "ok".

Those helpers deliberately are *not* `isRudderOk()`/`isWindOk()`: those fold in
the magnet/vane flag, and a board with a dead magnet is still perfectly
reachable for a re-calibration. The question there is only "is it talking".

Telnet also range-checks nothing on `d`/`k` — the sensor boards own those
bounds — but it does check the arguments are *numbers*, which is syntax rather
than policy. Without it `kfoo,bar` becomes a well-formed `0.0,0.0`, gets
refused at the board, and looks from the console exactly like nothing happened.

**Auto-prototype trap in `telnet.ino`:** any function taking a
`CustomClientType&` must be explicitly forward-declared in the block after the
`typedef`, or the build fails with "`CustomClientType` was not declared in this
scope". Arduino's prototype generator emits its own declaration ahead of the
typedef. **`static` does not exempt a function from this** — that is not
obvious and it does cost a build.

### Optimistic UI

When a button is pressed the display updates its **own** local state immediately
(snappy feedback) and *then* sends the command. To stop the next incoming
telemetry broadcast from clobbering that local change before the controller has
acted on it, the display suppresses the operator-controlled fields for
`LOCAL_COMMAND_SUPPRESS_MS` after a press (see `localCommandTime` in
`display/AutoPilot.cpp`). Keep this in mind when touching either the parser or the
button code.

**`nav_enabled` is parsed *outside* that suppression**, in both the display and
the plugin, and that is deliberate: the display can no longer set it, so there is
never a local value worth protecting, and suppressing it would only add lag to
the one field the operator most needs to see promptly — the kill switch.

## The motor-enable switch (`controller/motorenable.ino`)

The controller board's "Motor Enable" switch (U7) is the **sole** authority on
whether navigation is engaged. It is the motor's kill switch, so one action does
the whole job and the software state can never claim to be steering a motor with
no power.

The switch closes +5 V onto `Motor5V`, which feeds the motor header *and* a
10 k/10 k divider (`MOTOR_ENABLE_R1`/`R2`, filtered by `C1` 100 nF) into **`A6`**.
So `A6` reads ~2.5 V closed and a hard 0 V open — the lower leg is what makes
"open" a defined zero rather than a floating pin. **`D6`** drives a 1 kΩ into a
2N2222 (`Q1`) that low-side switches the lamp inside the switch: `D6` HIGH = lit.
The lamp follows `isNavigationEndabled()`, not the switch position — the switch
already shows its own position; what is worth showing is what the autopilot
believes.

**`A6` is GPIO13, an ADC2 channel, and ADC2 is shared with Wi-Fi.** On the
ESP32-S3 an arbiter gives Wi-Fi priority, and a read that loses it comes back
invalid — which arduino-esp32's `__analogReadRaw` turns into a plain `0`. Hence
the median-of-3 plus a 100 ms debounce; a spurious 0 is the fail-safe direction
(reads as "off"), so the filtering is about nuisance disengages, not safety.
`A0`–`A3` are ADC1 and unaffected; `A4`/`A5` are SDA/SCL.

`check_motor_enable()` runs from `control_task` every 10 ms tick, *before*
anything reads navigation state, and enforces `navigation_enabled ==` the
debounced switch every tick rather than only on edges — that per-tick
enforcement is what makes the switch authoritative by construction instead of
relying on nothing else ever writing the field. `setNavigationEnabled()` is
still only called on an actual change, because the disabled→enabled transition
does real work (seeds `heading_desired` from the current heading, clears
`compass_fallback`) that must not re-run 100 times a second.

**Consequence to keep in mind: the boat engages at power-up if the switch is
already closed.** That is the switch being authoritative, working as specified.

The telnet `p` output carries a `Motor enable: on (2497 mV)` line — the raw
divider reading is the thing to look at when the switch and the reported state
disagree.

## Firmware file map

**`controller/`** (Wi-Fi AP, broadcasts telemetry on 8888, listens for commands on 8889):
`controller.ino` (setup/loop, FreeRTOS tasks) · `compass.ino` (BNO08x IMU) ·
`gps.ino` (Adafruit GPS NMEA) · `garmin.ino` (Garmin NMEA-0183 in) ·
`pid.ino` (heading-error → steering correction) · `motor.ino` (steering motor) ·
`publish.ino` (`~APDAT` out) · `subscribe.ino` (`~APCMD` in) · `telnet.ino`
(separate command surface — see above) · `wifi.ino` (SoftAP) ·
`rudder.ino` (`~APRUD` in 8890, relay out 8891) · `wind.ino` (`~APWND` in 8892,
relay out 8893) · `AutoPilot.{h,cpp}` (state model).

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
ported wind maths) · `vane.ino` (AS5600 read + bow zero/trim + the mutex) ·
`anemometer.ino` (reed-switch ISR + rotation timing + speed calibration) ·
`temperature.ino` (DS18B20) · `publish.ino` (`~APWND` out, 8892) ·
`subscribe.ino` (relayed `~APCMD,v$`/`,d`/`,k` in, 8893) · `wifi.ino` (joins
SoberPilot, auto-reconnect).

## The rudder position sensor (`firmware/Arduino/rudder/`)

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
  whatever asked the controller for it: the OpenCPN plugin
  (`AutoPilotLink.cpp`, `SendCommand("z")`) or the telnet `z` command. Not the
  displays — see the two-command-surfaces note above.

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
`~APRUD` publish (`firmware/Arduino/rudder/publish.ino`, 50 Hz — see below), `~APCMD,z$`
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

**Done since:** the OpenCPN plugin's rudder box and "Center now" button
(`AutoPilotLink.cpp` sends `z`, `AutoPilotPanel.cpp` confirms first), and the
telnet `z` command.

**Publish rate (50 Hz, not the original 1 Hz):** unlike the human-readable
1 Hz `~APDAT` broadcast, rudder angle is meant to eventually feed a real
control loop (a cascaded heading→rudder-angle→motor loop, see
`.claude/docs/FutureUpgrades-WindAndRudder.md`), so it needs to keep pace with
the controller's existing 100 Hz heading PID (`compass.ino`'s `control_task`,
10 ms loop) rather than a display's refresh rate. 50 Hz was chosen as a
practical middle ground — within the same order of magnitude as the 100 Hz
loop it will eventually feed, without assuming Wi-Fi/UDP can sustain the full
100 Hz reliably (untested on the actual boat network).

## The masthead wind sensor (`firmware/Arduino/wind/`)

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
  `~APWND,<direction>,<speed_kn>,<speed_mps>,<bft>,<temp_c>,<vane_ok>,<temp_ok>,<speed_hz>$`
  at 5 Hz. `direction` is apparent wind angle 0–360° clockwise from the bow.
  As with the rudder board this is also the only way the controller can learn
  this board's IP.
- **UDP 8893**, controller → wind: `~APCMD,v$` (vane zero),
  `~APCMD,d<±degrees>$` (vane trim) and `~APCMD,k<slope>,<offset>$` (speed
  calibration). Verbs `v`/`d`/`k` because `dispatch_command()` switches on
  `buffer[0]` alone and `a/m/n/w/X/t/z` are taken; same reasoning that picked
  `z` for the rudder. **None is relayed by the controller yet** — three cases
  need adding alongside the existing `case 'z':`.

**`speed_hz` is on the wire for calibration, not steering.** Every other speed
field has the cup geometry *and* the fitted slope/offset baked in; rev/s is
upstream of all of it. Without it a calibration run could only be logged while
the calibration was identity, because otherwise you'd be fitting against
already-corrected data. It also allows re-deriving lambda if the head is ever
re-cupped. It is *appended*, following the `~APDAT` convention, so field order
stays stable for parsers.

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

**All calibration is runtime + NVS, for one reason.** None of it can be known
before the head is assembled, and reflashing a masthead unit is a genuinely bad
afternoon — so it all lives in the `"wind"` NVS namespace (`voffset`;
`calslope`/`caloffset`) rather than in build constants. Incoming values are
range-checked before they reach flash, using `!(x >= min && x <= max)` rather
than the naive form **on purpose**: every comparison against NaN is false, so
the obvious `x < min || x > max` would let a NaN parsed out of a malformed
datagram through and persistently poison the reading.

**Direction: `v` and `d` are not redundant, don't collapse them.**
- `v` (`calibrate_bow`) reads the vane and makes *its current position* zero.
  Precise, but needs a hand on the vane — bench-time only.
- `d` (`nudge_bow`) shifts the stored offset blind, in degrees. This is the
  in-service form, and it corrects errors `v` structurally *cannot*: `v` only
  ever fixes the vane's alignment to the sensor body, while the total error the
  boat experiences is that plus mast rotation relative to the hull plus
  aerodynamic bias in the mast/mainsail upwash. Neither of the latter is
  visible from the masthead.

`d` is **relative, not absolute**, because the measurement is a difference: you
find the offset by tacking (a constant offset makes the two tacks disagree;
the offset is *half* the spread in angle-off-the-bow between identically-trimmed
close-hauled runs) and that never yields an absolute encoder offset, only a
correction. Requests **accumulate** rather than coalesce in
`request_vane_nudge()` — two nudges are two trims, so overwriting would silently
drop one if both landed inside a `command_task` tick. Both work in whole AS5600
counts, keeping `voffset`'s meaning consistent; each nudge rounds once, so a run
of them drifts by at most ~0.04° apiece, far under what the tack test can
resolve.

**Speed: slope and offset are unrelated to the vane offset** despite the shared
word. The speed offset is a scalar in m/s absorbing bearing friction and
start-up threshold (real response is `v = a + b·n`, not a line through the
origin); the slope absorbs the gap between the nominal `ANEMOMETER_LAMBDA`
(which encodes cup shape/size and has no closed form) and this head's real one.
Fitted off-board — log against a reference at several steady speeds, take the
linear fit; the original project documents a car on a windless day with a GPS
app.

**`vaneRequestMux` vs `vaneMutex` — two locks, two jobs.** `vaneMutex` is a
recursive semaphore guarding the AS5600 and the live offset; `vaneRequestMux`
is a spinlock guarding only the command handoff from the AsyncUDP task, needed
because the nudge carries a value alongside its flag. Nothing blocking is ever
called while holding the spinlock.

**No 1-Wire library, and the trap that removed it.** `temperature.ino`
bit-bangs its single DS18B20 through `digitalRead`/`digitalWrite` rather than
using OneWire + DallasTemperature, and that is not a preference. OneWire's
ESP32 back end uses the number it is constructed with as a **raw GPIO bit
index** (`PIN_TO_BITMASK(pin)` is `(pin)`, then `GPIO.in >> pin`). On the Nano
ESP32 under the default Arduino Pin Numbering, `D4` is Arduino pin `4` while
the pad is `GPIO7` — so `begin()`'s `pinMode()` set up the right pad and every
bus operation drove `GPIO4`, which is `A3` and wired to nothing. Temperature
read as "no data" for as long as the library was in there.

Generalise it: writing `D4` instead of `7` is what makes this sketch immune to
the Pin Numbering setting, but **that only protects code going through the
Arduino API**. Any library reaching past it to the GPIO registers is broken
here whichever name it is handed, and it fails *silently* — a dead peripheral,
no compile or runtime error. Feeding such a library the GPIO number instead
does not fix it either, because its own `pinMode()` call would then configure
the wrong pad. Grep a candidate library for `GPIO.out_w1ts`/`GPIO.in` before
adding it.

Because the bus has exactly one device, dropping the library cost almost
nothing: there is no ROM search and no device table, every transaction is SKIP
ROM, and `report_empty_bus()` reports drive/rise-time/presence when the bus
comes up empty — the three things that separate an open DQ joint from a dead
part from a missing pull-up, none of which a voltmeter on the pads can tell
apart.

**`TEMPERATURE_CORRECTION_C` is 3.6, measured on this board, and it is mostly
not self-heating.** The original subtracts a flat 6.0 and calls it self-heating
compensation, but the arithmetic does not support that: ~1 mA while converting,
375 ms in every 500, is ~2.5 mW, and a TO-92 in still air is roughly 200 °C/W,
so the die runs about **0.5 °C** above its own package. The rest is the board —
the ESP32-S3 module runs warm centimetres away on the same small PCB, so the
sensor genuinely sits in air above ambient. The practical consequence is that
the constant is only weakly tied to the poll rate (halving it would move the
reading ~0.25 °C) and strongly tied to how heat leaves the board, so it will
want re-measuring once the head is sealed and in free air.

Calibrate it against an infrared thermometer aimed at the **flat face** of the
TO-92, not the rounded back: the die is bonded against the flat, and a first
attempt that read the back came out 1.0 °C cold. It is a single-point fit at
~21 °C, so an offset with no slope — untested near 0 °C or 35 °C. It is still a
build-time `#define` rather than a runtime NVS command like `v`/`d`/`k`, which
is arguably the wrong side of this project's own line for a masthead unit.

**Two things that look like arbitrary constants but aren't:**
- **`TEMPERATURE_INTERVAL_MS` is 500**, and what actually pins it there is the
  resolution: **11-bit** (375 ms conversion) has to finish inside the interval,
  where the default 12-bit takes 750 ms and would not. The original's claim
  that the correction constant is calibrated to the poll rate does *not* carry
  over — see below.
- **`ANEMOMETER_PERIOD_LIMIT_MS` is enforced in two places** — the ISR clamps
  intervals to it, `Wind::calculate()` then refuses to convert a period that
  reached it. It is defined once in `Wind.h` so the two layers can't drift.
  Genuinely-stopped cups are caught by the separate 3 s zero-wind timeout.

**Threading** is the rudder board's split, for the rudder board's reasons:
`sensor_task` (CORE_0) samples/calculates/publishes; `command_task` (CORE_1)
runs `check_wifi()`, `check_vane_calibration_request()` and
`check_speed_calibration_request()`, all of which block for a long time (an
association attempt, an NVS write). `vaneMutex` is mandatory for the same
two-Wire-transaction reason as `angleMutex`, and the AsyncUDP callback only
sets flags.

**Pin naming:** this sketch uses `D2`/`D3` rather than bare integers, so it is
correct under either Arduino IDE Pin Numbering setting — unlike the controller,
which is why that README carries a warning about it.

**Controller side (done).** `controller/wind.ino` mirrors
`controller/rudder.ino`: listens on 8892, remembers the sender's IP, stores the
payload via `AutoPilot::setWind()`, and `relay_wind_command()` forwards `v`/`d`/
`k` on 8893. `dispatch_command()` has the three cases; telnet has them too, plus
`z`. **The controller deliberately does not parse or validate `d`/`k`
arguments** — it passes the whole verb string through untouched. The wind board
owns those bounds (they are what protect its flash) and a second copy here
would be one more thing to keep in step.

`AutoPilot::setWind()` takes all eight fields in **one** call, not eight
setters: they only ever arrive together in one packet, and a single lock means
no reader can see half an update. `isWindOk()` (wind.ino, not the class) is the
combined flag — receive timeout AND vane flag — and deliberately does *not*
fold in `temp_ok`, since a dead DS18B20 costs nothing while wind angle and
speed keep working.

**`speed_hz` is parsed as optional.** It was appended to `~APWND` after the
first seven fields existed, so `process_wind_telemetry()` accepts a frame
without it and stores 0. Keep that tolerance when adding further trailing
fields — it is the same convention `~APDAT` follows.

**Downstream (done).** Wind is on `~APDAT` as eight trailing fields —
`<awa>,<aws_kn>,<wind_ok>,<twa>,<tws_kn>,<true_wind_ok>,<air_temp_c>,<temp_ok>`
— with `display/AutoPilot.{h,cpp}` and `autopilot_pi`'s
`AutoPilotState`/`ParseApdat()` carrying them too. Nothing *renders* them yet:
the TFT layout (`display/screen.ino`) and the plugin panel are unchanged, so
this is parsed-and-available, not displayed.

Only the display-facing subset travels. The m/s and Beaufort forms are
derivable from knots, and `speed_hz` is calibration data, not display data —
all three stay controller-side, on the telnet `p` line, which remains the place
to see the full wind state (and prints the rudder line, still the only
confirmation a `z` landed).

**True wind is derived on the controller** — `AutoPilot::getTrueWind()`, a
single call returning angle and speed together from one locked read, for the
same "no reader sees half an update" reason `setWind()` takes eight fields at
once. It is computed there rather than on each display so the two TFT head
units and the plugin can never disagree, and so there is one place to change
when a speed-through-water sensor eventually replaces SOG.

That SOG is the caveat worth repeating: the boat has no paddlewheel, so the
"true" wind carries current and leeway. Right for a display and for steering a
wind angle; wrong for polars or performance logs. `isTrueWindOk()` (wind.ino,
not the class — it needs the receive timeout, same as `isWindOk()`) is
`isWindOk()` **and** a GPS fix, because with no fix there is no boat speed to
subtract and the output would be the apparent wind wearing a true-wind label.
Below `GPS_SPEED_DEADBAND_KNOTS` (0.8 kn) `setSpeed()` zeroes the speed, so true
wind reads as apparent there — correct to within the deadband, and much better
than GPS noise at anchor swinging the reported angle.

**Buffer sizes are part of this change, not incidental to it.**
`controller/publish.ino` now uses `snprintf` into a 400-byte `DATA_SIZE`
(208 chars worst case), and `display/subscribe.ino`'s matching `DATA_SIZE` went to
400 with it. That one has to be **at least** the controller's: an oversized
datagram is dropped whole there, not truncated, so a lagging receive buffer
doesn't lose the new fields, it loses the entire frame and the display just
goes "not connected". Grow the receiver first.

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

Full instructions live in `firmware/Arduino/README.md`. The short version: each sketch has
a `sketch.yaml` defining a `nano` profile (board `arduino:esp32:nano_nora` + pinned
libraries), so `arduino-cli` installs everything itself — there is intentionally
**no** `firmware/Arduino/libraries/` folder.

```bash
cd firmware/Arduino/controller        # or firmware/Arduino/display
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

## The navigation computer (Raspberry Pi 5)

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

Source lives at `navigator/opencpn_plugin/autopilot_pi/`.  Full details in
`navigator/opencpn_plugin/autopilot_pi/README.md` — read it before touching plugin code.

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
[sep]  Mode  << 10  < 1  1 >  10 >>  Nav On/Off (read-only)
```

Left column total height (198 px) = mid+right data (160 px) + date bar (38 px)
so all column tops and bottoms are flush.

### Controls

Single button row: **Mode · << 10 · < 1 · 1 > · 10 >> · Nav On/Off**.
All disabled when no link.  Mode + adjust buttons disabled when nav is off.
**Nav On/Off is an indicator, permanently disabled** — navigation is the
controller's motor-enable switch only.  It is kept as a `wxButton` rather than
deleted so all three dock layouts keep their fixed-pixel geometry.
Mode toggles 1 ↔ 2 (goes to 2 only if `waypoint_set`; otherwise stays at 1).
Adjust buttons auto-switch controller from mode 2 → 1 before applying delta.

**Send WP** button (embedded in data area, bottom-right): enabled only when
connected AND OpenCPN has an active route leg.  Sends `~APCMD,w<lat>,<lon>$`
without changing mode — user controls mode separately.

### Build

```bash
cd navigator/opencpn_plugin/autopilot_pi
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

## CAD & enclosures (`cad/`)

One directory per unit (`cad/wind/`, `cad/rudder/`, …), each holding its `README.md`
(plus `Assembly.md` where there is one) and the `.FCStd` documents **flat in the unit
directory** — there is no `FreeCad/` subdirectory any more.

**No STLs live in the repo.**  The `3D-Parts/` directories are gone; meshes are
exported from the `.FCStd` on demand and written outside the repo.  Never commit one —
a checked-in STL is a stale copy of a model that has moved on.  (Historical exception:
`fane_support_big` / `fane_support_smal` in `cad/wind/` have no FreeCAD source and only
exist in git history at `3b8b956:cad/wind/3D-Parts/`.)

Two families of parts live here and they are edited completely differently:

- **`cad/wind/`** — single `Part::Feature` objects holding a **baked BREP shape** with
  no feature tree (imported from the upstream Yachta IGES masters).  Nothing to
  re-parameterise; every change is a boolean operation on the shape.
- **`cad/rudder/`** — authored from scratch as **parametric PartDesign bodies**: a
  `Params` spreadsheet whose aliases drive every sketch constraint and pad length, all
  sketches fully constrained.  Edit these by changing a spreadsheet cell, not by
  boolean surgery.

Prefer the second style for anything new.

### ALWAYS author the GUI data — never ship a headless `.FCStd`

`freecadcmd` builds a perfectly good `Document.xml` and **no `GuiDocument.xml`**.  A
file saved that way has no view providers, no visibility state and no camera: the user
opens it in FreeCAD and sees an empty 3D view.  The geometry is in there, but as far as
they are concerned the work does not exist.

So whenever a script creates or modifies a `.FCStd`, it must also author the GUI side.
`FreeCADGui` runs fine without a display if you bring the main window up offscreen:

```python
# run with QT_QPA_PLATFORM=offscreen
import FreeCADGui
FreeCADGui.showMainWindow()      # BEFORE App.newDocument() / openDocument()
```

Then, before saving:

- hide every sketch, datum and origin feature, and every PartDesign feature that is not
  `body.Tip`;
- show `body` and `body.Tip`, and set `ShapeColor` on them;
- splice a `<Camera settings="OrthographicCamera { … }"/>` element into
  `GuiDocument.xml` (rewrite the zip after `saveAs`).  Coin wants a **normalised
  axis + angle**, not a raw quaternion — `App.Rotation(...).Axis` normalised plus
  `.Angle`.  FreeCAD's axonometric rotation is
  `App.Rotation(0.4247082, 0.1759200, 0.3398509, 0.8204732)`.  Without this the
  document opens on the default camera and the part can be off-screen.

**Verification is not optional and is one command:**

```bash
unzip -l cad/<unit>/<Part>.FCStd | grep -E "GuiDocument|ShapeAppearance"
```

No `GuiDocument.xml` in that listing means the file is not finished.  Re-open it in a
GUI-enabled session and confirm `Gui.getDocument(d).getObject(name).Visibility` is
`True` for the body and its tip and `False` for the sketches.

### Scripting gotchas

- Binary is `/Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd` (not on
  PATH).  `print()` output is swallowed — have the script write a log file and `cat` it.
  Wrap the whole script in `try/except` + `traceback.format_exc()` into that log, or
  failures surface only as "Unknown exception while processing file".
- The bundled interpreter (`.../Resources/bin/python`) has numpy + matplotlib; the
  system `python3` does not.
- Offscreen Qt has **no OpenGL**, so `createViewer()` / `saveImage()` fail.  For a
  visual check, tessellate with `MeshPart.meshFromShape()` and render the triangles
  with matplotlib (`matplotlib.use("Agg")`).
- FreeCAD 1.x renamed the sketch attachment property to **`AttachmentSupport`**.
  Attach sketches to `XY_Plane` with an `AttachmentOffset` in Z rather than to a face —
  that sidesteps topological naming entirely and keeps the tree editable.
- Drive dimensions from the spreadsheet by naming the constraint first:
  `sk.renameConstraint(sk.ConstraintCount - 1, 'hub_r')` then
  `sk.setExpression('.Constraints.hub_r', 'Params.hub_dia / 2')`.
- A same-sheet derived cell is a plain formula string — `sh.set('B7', '=plate_size -
  arm_w')`.  `sh.setExpression('B7', ...)` raises `Property 'B7' not found`.
- `Gui.getDocument(d).ActiveObject` is read-only; there is no need to set an active
  body when scripting.
- Overlapping profiles in one sketch will not pad.  Build an X/cross plate as separate
  pads that fuse (a hub circle plus two crossed obrounds), not one self-intersecting
  wire.
- `saveAs` leaves a `*.FCBak` next to the file — delete it so it does not land in git.
- Fusing a prism to extend an outline also fills any internal void it spans.  Cut the
  added prism against the original envelope first (e.g. `sq.cut(cylinder(R - 0.1))`) so
  pockets and bores survive.

### When reverse-engineering an STL

Mesh in, model out: slice the mesh along its axis
(`shape.slice()`, or intersect triangles with a plane yourself), least-squares-fit each
loop to a circle, and read the feature dimensions off the fits — do **not** import the
mesh and ship that.  Verify by sampling every mesh vertex and triangle centroid and
measuring `Part.Vertex(p).distToShape(solid)`; on the rudder test parts that lands at
0.005–0.04 mm, which is the mesh's own chord error and well under print resolution.
