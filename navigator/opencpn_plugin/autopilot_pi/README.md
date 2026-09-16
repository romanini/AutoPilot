# autopilot_pi — OpenCPN Plugin

An OpenCPN plugin that acts as a software display unit for the AutoPilot system.
It listens to the same `~APDAT` UDP telemetry the physical display units receive,
mirrors their panel layout on screen, and sends `~APCMD` commands back to the
controller — all over the existing Wi-Fi protocol with no changes to the
controller or display firmware.

---

## How it fits into the system

```
                    SoberPilot Wi-Fi (10.20.1.x)
                              │
          ┌───────────────────┼────────────────────┐
          │                   │                    │
   Controller (AP)     Display unit(s)     RaspberryPi 5 / OpenCPN
   10.20.1.1           10.20.1.x           10.20.1.x
   broadcast ~APDAT    listen + buttons    listen + autopilot_pi
   on UDP 8888         send ~APCMD         sends ~APCMD
                       on UDP 8889         on UDP 8889
```

The plugin is a **fourth client** on the SoberPilot network.  It joins the same
broadcast group as the physical display units: it receives every `~APDAT` packet
and can send any `~APCMD` command.  The controller treats it identically to any
other display.

---

## Panel layout

The floating panel visually mirrors the physical HX8357 TFT display unit.
All colours match the TFT palette exactly.  Fixed pixel layout at 480 × ~275 px
(no resize larger than content; shrinking smaller shows scrollbars).

```
┌─────────────────────────────────────────────────────────────────────────┐
│ LEFT COL (CYAN/YELLOW, 160 × 198 px)  │ MID COL (160 px) │ RIGHT (160) │
│                                        │                   │             │
│  [CYAN]  Speed kn                      │ [LAVENDER]        │ [CYAN]      │
│            3.20                        │  Destination      │  Distance nm│
│                                        │    Compass        │    2.50     │
│  [YELLOW] Heading                      │    180.0°         │             │
│            182.4°                      │                   │ [GREEN]     │
│                                        │ [ORANGE]          │  Course     │
│  [YELLOW] Pitch                        │  Bearing          │    183.1°   │
│             1.2°                       │    180.0°         │             │
│                                        │    0.4° R         │ [GREEN]     │
│  [YELLOW] Roll                         │                   │  Location   │
│             0.8°                       │                   │  37.812345  │
│                                        │                   │ -122.423456 │
│  [YELLOW] Stability                    ├───────────────────┴─────────────┤
│             Stable                     │ [WHITE] Date / Time │ [Send WP] │
│                                        │  6/19/26  14:30     │  button   │
│                                        │  GPS(8)             │           │
├────────────────────────────────────────┴─────────────────────────────────┤
│  ─────────────────────────────────────────────────────────────────────   │
│   Mode    << 10    < 1    1 >    10 >>                                   │
└──────────────────────────────────────────────────────────────────────────┘
```

**Column colours** (matching TFT palette):

| Cell | Colour |
|------|--------|
| Speed, Distance | Cyan `#00FFFF` |
| Heading, Pitch, Roll, Stability | Yellow `#FFFF00` |
| Destination | Lavender `#F7AEFF` |
| Bearing, Bearing correction | Orange `#FF824A` |
| Course, Location | Green `#7BFF42` |
| Date/Time, GPS fix | White `#FFFFFF` |

Each cell has a 2 px coloured border, black interior, and a small coloured
title pill (coloured background only as wide as the title text — matching the
TFT's `fillRect` behind the title string).

**Date/Time bar** spans the middle column + 1/3 of the right column (213 px).
The remaining 2/3 of the right column bottom (107 px) holds the **Send WP**
button, positioned directly below the Location cell.

**Destination font** is large (17 pt, same as Speed/Distance) when in compass
mode (mode 1) showing just the desired heading, and smaller (12 pt) in waypoint
mode (mode 2) where it shows coordinates.

---

## Controls

Single button row below the data area:

| Button | Active when | Action |
|--------|-------------|--------|
| **Mode** | Connected + nav enabled | Toggles mode 1 ↔ 2. Goes to mode 2 only if a waypoint is set on the controller; otherwise stays at mode 1. |
| **<< 10** | Connected + nav enabled | Adjust desired heading −10° (switches to compass mode first if in mode 2) |
| **< 1** | Connected + nav enabled | Adjust desired heading −1° |
| **1 >** | Connected + nav enabled | Adjust desired heading +1° |
| **10 >>** | Connected + nav enabled | Adjust desired heading +10° |
| **Send WP** | Connected + active OpenCPN route leg | Send the active waypoint coordinates to the controller. Does not change mode. |
| **Settings** | Connected | Opens the rudder/wind calibration window (see below). |

Navigation itself is not controllable from this panel at all — it is engaged
and disengaged only by the motor-enable switch on the controller board
(`controller/motorenable.ino`), which is also the motor kill switch. Whether
nav is on is shown in the **Mode** cell (`Disabled` when off); all buttons
above except Send WP and Settings are disabled while nav is off.

All buttons are disabled when there is no link (no `~APDAT` received within 10 s).

### Settings dialog

**Settings** opens a modeless `AutoPilotSettingsDialog` window with the rudder
and wind-vane calibrations documented in the autopilot skill's rudder/wind
sensor sections. It's modeless (`Show()`, not `ShowModal()`) rather than a
blocking dialog so the trim buttons can be tapped repeatedly while watching
AWA update live underneath. Only one instance exists at a time — clicking
Settings again while it's open just raises it.

| Control | Sends | Notes |
|---|---|---|
| **Zero Rudder** | `~APCMD,z$` | Recalibrates dead-center from the rudder's current physical position. Confirmed via dialog. Disabled while nav is enabled — recalibrating under a live PID loop would yank the helm. |
| **Zero Vane** | `~APCMD,v$` | Zeroes the wind vane at its current physical position. Confirmed via dialog — needs a hand holding the vane aligned to the bow. |
| **Trim ±1°/±10°** | `~APCMD,d<±degrees>$` | Nudges the stored vane offset. No confirmation — designed for repeated taps (e.g. while tacking to find the correction). Requests accumulate on the wind board. |
| **Wind Speed Calibration → Apply** | `~APCMD,k<slope>,<offset>$` | Overwrites the anemometer's linear-fit slope/offset outright. Confirmed via dialog echoing the values, since it's a silent full overwrite with no readback. Values come from an external calibration run (log against a reference speed source), not from anything visible in this dialog. |

None of `z`/`v`/`d`/`k` have an ack, so the dialog shows *live readings*
(current rudder angle, current AWA/AWS) for context but can never show what
offset/slope is currently stored on either board — same limitation the rest
of this project's calibration commands already live with.

**PID auto-tune belongs in this dialog too, and is not built yet** — see
[Planned work: PID auto-tune controls](#planned-work-pid-auto-tune-controls) at
the end of this file for the full specification.

### Optimistic UI

Button presses update the panel **immediately** (before the controller confirms),
then suppress the affected telemetry fields for 2 seconds so the next incoming
`~APDAT` broadcast does not flicker the display back to the pre-command state.
This matches the behaviour of the physical display unit (`localCommandTime`
suppression in `display/AutoPilot.cpp`).

---

## Sending a waypoint

1. Drop a mark on the OpenCPN chart and create a route to it (right-click →
   "Route to here"), or build a multi-leg route via the Route tool.
2. Right-click the route → **Activate**.  OpenCPN fires `SetActiveLegInfo`
   into the plugin, which resolves the destination waypoint lat/lon via
   `GetActiveWaypointGUID()` + `GetSingleWaypoint()`.
3. The **Send WP** button becomes active.  Click it — the plugin sends
   `~APCMD,w<lat>,<lon>$` to the controller.  The controller stores the
   waypoint and sets `waypoint_set = true`.
4. Click **Mode** when ready to switch the controller to waypoint-navigate
   (mode 2).  The Destination cell will show the waypoint coordinates and
   the Bearing cell will track the course to the waypoint.

---

## Architecture

### Source files

```
autopilot_pi/
├── flatpak/
│   └── org.opencpn.OpenCPN.Plugin.autopilot.yaml   # Flatpak extension manifest
├── include/
│   ├── version.h                    # plugin v0.1, API v1.17
│   ├── autopilot_pi.h               # AutoPilotPlugin — opencpn_plugin_117 subclass
│   ├── AutoPilotLink.h              # UDP socket layer; AutoPilotState struct
│   ├── AutoPilotPanel.h             # wxScrolledWindow panel
│   └── AutoPilotSettingsDialog.h    # modeless rudder/wind calibration window
└── src/
    ├── autopilot_pi.cpp             # plugin lifecycle, toolbar, SetActiveLegInfo
    ├── AutoPilotLink.cpp            # receive/parse ~APDAT, send ~APCMD, optimistic state
    ├── AutoPilotPanel.cpp           # BuildUI (fixed-pixel layout), UpdateFromState
    └── AutoPilotSettingsDialog.cpp  # Zero Rudder / Zero Vane / vane trim / speed cal
```

### Class responsibilities

**`AutoPilotPlugin`** (`opencpn_plugin_117`)
- Entry point OpenCPN loads via `create_pi()`.
- Creates the `wxAuiManager` floating pane sized exactly to the panel content.
- Handles the toolbar "AP" button (force-floats the pane if it was docked).
- Receives `SetActiveLegInfo` from OpenCPN when the active route leg changes;
  resolves the waypoint lat/lon and passes it to `AutoPilotPanel`.
- Returns `WANTS_TOOLBAR_CALLBACK | INSTALLS_TOOLBAR_TOOL | USES_AUI_MANAGER | WANTS_PLUGIN_MESSAGING`.

**`AutoPilotLink`** (`wxEvtHandler`)
- Owns two `wxDatagramSocket`s:
  - Receive: bound to `0.0.0.0:8888`, receives controller broadcast.
  - Send: ephemeral port, unicasts to `10.20.1.1:8889`.
- A 250 ms `wxTimer` drives `DrainSocket()` which reads all pending packets.
- `ParsePacket()` parses `~APDAT` into `AutoPilotState` and calls
  `AutoPilotPanel::UpdateFromState()`.
- Connection timeout: 10 s with no packet → `IsConnected()` returns false.
- **Optimistic state**: `SendMode()` and `SendAdjust()` each update `m_state`
  locally, call `UpdateFromState()` immediately, and set a 2 s suppress window
  so the next telemetry packet does not overwrite the locally-commanded fields
  (`mode`, `heading_desired`, `bearing`, `bearing_correction`). `nav_enabled` is
  deliberately **not** among them: the plugin cannot set it, so it is always
  taken straight from telemetry.
- `AutoPilotState` struct mirrors every field in the `~APDAT` sentence.

**`AutoPilotPanel`** (`wxScrolledWindow`)
- `BuildUI()` creates a fixed-pixel 3-column data grid using `wxBoxSizer`
  and `MakeBox()` (coloured border → black interior → coloured title pill).
  Virtual size is set via `FitInside()` so the AUI pane can be sized exactly.
- `UpdateFromState()` refreshes all labels and button enable/disable states
  on every telemetry tick (~4 Hz).
- `SetNavigateTarget()` called by the plugin when the active OpenCPN waypoint
  changes; enables/disables the Send WP button accordingly.
- Owns (non-owning raw pointer) the `AutoPilotSettingsDialog` opened by the
  Settings button; forwards each `UpdateFromState()` call to it while open, and
  explicitly destroys it in `SetDockMode()` before `DestroyChildren()` — it is
  parented to the panel, so a dock-mode switch would otherwise destroy it out
  from under that pointer without going through its close callback.

**`AutoPilotSettingsDialog`** (`wxDialog`, modeless)
- Rudder/wind calibration window opened from the main panel's Settings button.
- `UpdateFromState()` refreshes live rudder-angle/AWA-AWS readouts and the
  Zero Rudder enable state (disabled while nav is enabled) on every tick.
- Zero Rudder / Zero Vane both confirm via `wxMessageDialog` before sending;
  the vane trim buttons and speed-cal Apply do not (trim is designed for
  repeated taps, and Apply already confirms the typed values before sending).
- Calls `SetCloseCallback()` on the owning panel so the panel's pointer is
  cleared the moment the window closes itself.

### Key constants (AutoPilotPanel.cpp)

| Constant | Value | Meaning |
|----------|-------|---------|
| `kColW` | 160 px | Width of each of the three columns |
| `kH_Spd` | 50 px | Speed cell height |
| `kH_Hdg/Ptc/Rol/Stb` | 37 px each | IMU cell heights (left col total = 198 px) |
| `kH_Dst` | 80 px | Destination cell height |
| `kH_Brg` | 80 px | Bearing cell height (mid col total = 160 px) |
| `kH_Dis` | 50 px | Distance cell height |
| `kH_Crs` | 45 px | Course cell height |
| `kH_Loc` | 65 px | Location cell height (right col total = 160 px) |
| `kH_Bar` | 38 px | Date/Time bar height |
| `kDateW` | 213 px | Date bar width = kColW + kColW/3 |
| `kWpBtnW`| 107 px | Send WP button width = kColW×2 − kDateW |

Left column total (198 px) = right block total (160 px data + 38 px bar)
so all column tops and bottoms align horizontally.

---

## Building

**Pre-requisites (once on the Raspberry Pi 5):**

```bash
sudo apt install flatpak-builder cmake g++
flatpak install flathub org.freedesktop.Sdk//25.08
```

**Build and install:**

```bash
cd navigator/opencpn_plugin/autopilot_pi
flatpak-builder --user --install --force-clean \
    build-dir flatpak/org.opencpn.OpenCPN.Plugin.autopilot.yaml
```

The built `.so` is installed to
`/app/extensions/autopilot/lib/opencpn/libautopilot_pi.so` inside the Flatpak
sandbox, which OpenCPN sees via the extension merge path.

**After a crash (plugin blacklisted):**

OpenCPN writes a zero-byte stamp file before loading each plugin.  If the plugin
crashes the stamp remains and OpenCPN refuses to load it on the next launch:

```bash
rm ~/.var/app/org.opencpn.OpenCPN/config/opencpn/load_stamps/libautopilot_pi
```

**Verify:**

```bash
flatpak info org.opencpn.OpenCPN.Plugin.autopilot
# Launch OpenCPN → Options → Plugins → "AutoPilot" should appear and be enabled
```

---

## Live verification checklist

With the Raspberry Pi 5 on the SoberPilot network and the controller running:

1. Enable the plugin in OpenCPN → Options → Plugins.
2. Click the **AP** toolbar button — floating panel appears, sized to content.
3. Within ~1 s the data cells populate (controller broadcasts every ~1 s).
4. Heading, pitch, roll, bearing should match the physical display unit.
5. Press **< 1** — desired heading decrements by 1°; physical display updates too.
6. Flip the **motor-enable switch** on the controller — the Mode cell drops
   `Disabled` within ~1 s and the Mode/adjust buttons become active. Nothing
   in the plugin (or on a display) can enable navigation.
7. Create and activate a route in OpenCPN — **Send WP** button activates.
8. Click **Send WP** — controller receives waypoint; physical display shows
   `waypoint_set = true` in Destination cell.
9. Click **Mode** — controller switches to mode 2 (waypoint navigate);
   Bearing cell tracks course to waypoint.
10. Click **Settings** — the calibration window opens with live rudder-angle
    and AWA/AWS readings; while nav is enabled, Zero Rudder is disabled but
    Zero Vane/Trim/speed-cal Apply are not.
11. Tap a vane **Trim** button — the wind board's reported AWA should shift by
    that amount on the next `~APWND`/`~APDAT` tick.
12. Click **Zero Rudder** with nav disabled — confirm dialog appears; after
    confirming, the rudder-angle readout should settle near 0° (dead-center).


---

## Planned work: PID auto-tune controls

**Status: specified, not implemented.** This section is a work order — build it
in this dialog and test it on the boat.

### Why this is now urgent

Auto-tune used to be driven from the physical display unit: hold **MODE** for
5 s while navigation was disabled to arm it, then press the **AUX** button to
start. Both were removed from `firmware/Arduino/display/` when AUX became the
screen backlight, and `send_autotune()` went with them.

So right now **nothing can start or abort a tune.** Telnet's `pat`
(`controller/telnet.ino`) arms only — there is no telnet verb for start or
abort — and this plugin has never had auto-tune at all; it explicitly skips the
field in `ParseApdat()`. The controller's `t` verb is untouched and fully
working on the UDP surface (`case 't'` in `controller/subscribe.ino`), so this
is purely a missing UI, but the capability is unreachable until it exists.

### What the controller actually does

From `controller/autotune.ino`. `autoTuneState` is `0 = idle`, `1 = ready
(armed)`, `2 = running`, and it is published in `~APDAT`.

| Command | Verb | Accepted only when | Effect |
|---|---|---|---|
| Arm | `~APCMD,t1$` | navigation **disabled** AND state `0` | state → `1`, starts a 30 s watchdog |
| Start | `~APCMD,t2$` | state `1` | state → `2`, captures the current heading as the tune setpoint |
| Abort | `~APCMD,t0$` | state `1` or `2` | state → `0`, motor stopped |

Every one of these is a **silent no-op** if the transition is not valid from
the current state — `autotune_try_arm()` and `autotune_try_start()` just return
`false`. Like every other `~APCMD` there is no ack.

Running, the relay bangs the rudder to a fixed ±10°
(`AUTOTUNE_RELAY_AMPLITUDE_DEG`) either side of the heading it started at,
measures the resulting oscillation, and computes new Kp/Ki by Tyreus-Luyben.
It ends on its own after 6 half-cycles, or aborts at 90 s
(`AUTOTUNE_MAX_DURATION_MS`) or if the heading strays 60°
(`AUTOTUNE_ABORT_ERROR_DEG`). On success the new gains are applied and written
to flash immediately by `set_pid_gains()`.

### The operator flow, including the step that is easy to get wrong

1. Motor-enable switch **OFF** (navigation disabled) — arming is refused otherwise.
2. **Arm.** State → `1`. A 30 s watchdog (`AUTOTUNE_READY_TIMEOUT_MS`) now runs.
3. **Flip the motor-enable switch ON.** This is the non-obvious step: that
   switch is what puts +5 V on `Motor5V` and therefore on the motor
   (`controller/motorenable.ino`), so with it off the relay has nothing to
   swing the helm with and the tune would measure a boat that never turns.
   Arming requires the switch off; *running* requires it on.
4. **Start**, within the 30 s window. `control_task` (`controller.ino`) hands
   the wheel to `autotune_loop()` whenever state is `2`, ahead of and instead
   of the normal navigate path, regardless of navigation state.
5. It finishes or aborts by itself; **Abort** is available throughout.

**Verify step 3 early in testing.** It follows directly from the code — the
autotune branch in `control_task` sits before the `navigating` check, and the
motor is dead without the switch — but this sequence has never actually been
run, because the display flow it replaces was never exercised on the water
either. If arming turns out to be refused or the tune to abort when the switch
is flipped, that is the first thing to re-read.

**Safety note for the end of a tune.** `autotune_finish()` stops the motor and
sets state back to `0`, after which `control_task` falls straight through to
the normal navigating branch — and navigation is *enabled* at that point,
because step 3 turned it on. So the boat resumes autopilot steering to
`heading_desired` the instant the tune ends, with the brand-new gains. Make
sure the test is run somewhere that is safe for, and the operator is expecting.

### What to build

**`include/AutoPilotLink.h` / `src/AutoPilotLink.cpp`**

- Add `int autotune_state;` to `AutoPilotState`, next to `nav_source`.
- In `ParseApdat()`, replace
  `nextInt();  // autoTuneState — not used by this plugin, skip over`
  with `s.autotune_state = nextInt();`. Field position is unchanged — this is
  reading a field that is already on the wire, not adding one.
- Add `void SendAutoTune(int state);`, implemented as
  `SendCommand(wxString::Format("t%d", state));` — same one-liner shape as
  `SendZeroRudder()` and friends.

**Do NOT give `autotune_state` the optimistic-update treatment.** Every other
command in this plugin updates `m_state` locally and suppresses the field for
2 s, and that is wrong here: the controller *validates* these transitions and
silently refuses invalid ones, so an optimistic "ARMED" would be a confident
lie for two full seconds in exactly the case where the operator most needs the
truth — with a helm about to swing. Parse it straight through, outside
`m_suppress_until_ms`, and let the controller's own state drive the UI. This is
the same call already made for `autoTuneState` in
`firmware/Arduino/display/AutoPilot.cpp`, for the same reason.

**`include/AutoPilotSettingsDialog.h` / `src/AutoPilotSettingsDialog.cpp`**

A "PID Auto-Tune" section below the wind-speed calibration:

| Widget | Behaviour |
|---|---|
| Status text | `Idle` / `ARMED — start within Ns` / `RUNNING…` / `No link` |
| **Arm** button | Enabled when connected AND `!nav_enabled` AND state `0`. No confirmation — arming alone moves nothing |
| **Start** button | Enabled when connected AND state `1`. **Confirm** — this is the one that throws the helm ±10° |
| **Abort** button | Enabled when connected AND state ≠ `0`. No confirmation — never make someone confirm a stop |

Mirror the enable/disable rules on the controller's own guards rather than
inventing new ones, so a greyed button and a silent no-op can never disagree.

The countdown in the ARMED status is display-only: derive it from a local
timestamp taken when the state was first seen to go `0` → `1`. **The plugin
must not expire the arm itself** — `autotune_check_ready_timeout()` on the
controller owns that, and a second timer here would only create a window where
the two disagree. Show the count reaching zero and wait for the controller to
say `0`.

`UpdateFromState()` is already forwarded to this dialog every telemetry tick by
`AutoPilotPanel`, so no new plumbing is needed for the refresh.

### Testing

Add to the live verification checklist above:

13. Open **Settings** with the motor-enable switch **off**. Auto-tune status
    reads `Idle`; **Arm** is enabled, **Start** and **Abort** greyed.
14. Turn the motor-enable switch **on**. **Arm** greys out within ~1 s
    (arming needs navigation disabled). Turn it back off.
15. Click **Arm**. Status goes to `ARMED` with a countdown; **Start** enables.
16. Leave it alone for 30 s. The controller expires the arm — status returns to
    `Idle` on its own, with no help from the plugin.
17. **Arm** again, flip the motor-enable switch **on**, then click **Start** and
    confirm. Status goes to `RUNNING…` and the helm should begin swinging.
18. Click **Abort** mid-tune — motor stops, status returns to `Idle` within ~1 s.
19. A full tune (steps 17 without aborting): expect it to end by itself after
    roughly 3 oscillation periods, and the boat to resume steering immediately
    on the new gains. Confirm the new Kp/Ki over telnet (`p`).

Steps 17-19 need open water and a hand on the motor-enable switch.

### Adjacent cleanup, optional and separate

`ParseApdat()` currently includes `s.nav_enabled` in its
`m_suppress_until_ms` block. That looks like a leftover: this plugin has no
Nav On/Off button and cannot author the field, so there is no local value to
protect, and suppressing it only adds up to 2 s of lag to the kill-switch
indicator — the one field the operator most needs promptly. The display sketch
already parses `nav_enabled` outside its suppression window with exactly that
reasoning written next to it (`firmware/Arduino/display/AutoPilot.cpp`). Worth
matching, but it is not part of the auto-tune change and should land on its own.
