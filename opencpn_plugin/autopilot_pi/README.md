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
   Controller (AP)     Display unit(s)     OrangePi / OpenCPN
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
│   Mode    << 10    < 1    1 >    10 >>    Enable/Disable                 │
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
| **Enable / Disable** | Connected | Toggle navigation enabled/disabled. Label reflects current state. All other buttons disabled when nav is off. |
| **Send WP** | Connected + active OpenCPN route leg | Send the active waypoint coordinates to the controller. Does not change mode. |

All buttons are disabled when there is no link (no `~APDAT` received within 10 s).

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
│   ├── version.h          # plugin v0.1, API v1.17
│   ├── autopilot_pi.h     # AutoPilotPlugin — opencpn_plugin_117 subclass
│   ├── AutoPilotLink.h    # UDP socket layer; AutoPilotState struct
│   └── AutoPilotPanel.h   # wxScrolledWindow panel
└── src/
    ├── autopilot_pi.cpp   # plugin lifecycle, toolbar, SetActiveLegInfo
    ├── AutoPilotLink.cpp  # receive/parse ~APDAT, send ~APCMD, optimistic state
    └── AutoPilotPanel.cpp # BuildUI (fixed-pixel layout), UpdateFromState
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
- **Optimistic state**: `SendMode()`, `SendNavEnable()`, `SendAdjust()` each
  update `m_state` locally, call `UpdateFromState()` immediately, and set a
  2 s suppress window so the next telemetry packet does not overwrite the
  locally-commanded fields (`nav_enabled`, `mode`, `heading_desired`,
  `bearing`, `bearing_correction`).
- `AutoPilotState` struct mirrors every field in the `~APDAT` sentence.

**`AutoPilotPanel`** (`wxScrolledWindow`)
- `BuildUI()` creates a fixed-pixel 3-column data grid using `wxBoxSizer`
  and `MakeBox()` (coloured border → black interior → coloured title pill).
  Virtual size is set via `FitInside()` so the AUI pane can be sized exactly.
- `UpdateFromState()` refreshes all labels and button enable/disable states
  on every telemetry tick (~4 Hz).
- `SetNavigateTarget()` called by the plugin when the active OpenCPN waypoint
  changes; enables/disables the Send WP button accordingly.

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

**Pre-requisites (once on the OrangePi):**

```bash
sudo apt install flatpak-builder cmake g++
flatpak install flathub org.freedesktop.Sdk//25.08
```

**Build and install:**

```bash
cd opencpn_plugin/autopilot_pi
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

With the OrangePi on the SoberPilot network and the controller running:

1. Enable the plugin in OpenCPN → Options → Plugins.
2. Click the **AP** toolbar button — floating panel appears, sized to content.
3. Within ~1 s the data cells populate (controller broadcasts every ~1 s).
4. Heading, pitch, roll, bearing should match the physical display unit.
5. Press **< 1** — desired heading decrements by 1°; physical display updates too.
6. Press **Enable** — navigation enables; all other buttons become active.
7. Create and activate a route in OpenCPN — **Send WP** button activates.
8. Click **Send WP** — controller receives waypoint; physical display shows
   `waypoint_set = true` in Destination cell.
9. Click **Mode** — controller switches to mode 2 (waypoint navigate);
   Bearing cell tracks course to waypoint.
