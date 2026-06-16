# autopilot_pi — OpenCPN plugin

An OpenCPN plugin that talks to the AutoPilot controller over the existing
`~APDAT`/`~APCMD` UDP protocol.  Shows a dockable status panel mirroring the
physical display unit and lets the sailor push the currently active OpenCPN
waypoint to the controller as a navigate-to target.

## What it does

- **Receives** `~APDAT` broadcasts (UDP port 8888) and displays live:
  heading, desired heading, bearing, bearing correction, mode, navigation
  enabled/disabled, GPS fix + satellites, position, waypoint, distance,
  speed, course, and current active OpenCPN leg.
- **Sends** `~APCMD` commands (unicast UDP port 8889 → controller `10.20.1.1`):
  - Mode: compass-hold (`m1`) / waypoint-navigate (`m2`)
  - Navigation enable/disable (`n1` / `n0`)
  - Port/starboard heading adjust ±1° / ±10° (`a-1.00`, `a10.00`, …)
  - Waypoint (`w<lat>,<lon>`) followed by mode 2
- **"Navigate to active waypoint"** button: reads the active OpenCPN waypoint
  via `GetActiveWaypointGUID()` + `GetSingleWaypoint()`, sends it to the
  controller, and switches the controller to waypoint-navigate mode.

## Source layout

```
autopilot_pi/
├── CMakeLists.txt                # standalone cmake build
├── flatpak/
│   └── org.opencpn.OpenCPN.Plugin.autopilot.yaml
├── include/
│   ├── version.h                 # API + plugin version constants
│   ├── autopilot_pi.h            # main plugin class (opencpn_plugin_117)
│   ├── AutoPilotLink.h           # UDP comms + AutoPilotState struct
│   └── AutoPilotPanel.h          # dockable wxPanel
└── src/
    ├── autopilot_pi.cpp
    ├── AutoPilotLink.cpp
    └── AutoPilotPanel.cpp
```

## Protocol

Mirrors `controller/publish.ino` and `controller/subscribe.ino` exactly:

| Direction | Socket | Format |
|-----------|--------|--------|
| Controller → plugin | Receive broadcast on `0.0.0.0:8888` | `~APDAT,<25 fields>$` |
| Plugin → controller | Unicast to `10.20.1.1:8889` | `~APCMD,<cmd>$` |

Field order for `~APDAT` is documented in `controller/publish.ino`.

## Phase 5 — building as a Flatpak extension

**Pre-requisites (install once on the OrangePi):**

```bash
# Build tools
sudo apt install flatpak-builder cmake g++

# Flatpak SDK (large download — several GB)
flatpak install flathub org.freedesktop.Sdk//24.08
flatpak install flathub org.freedesktop.Platform//24.08

# If the OpenCPN runtime doesn't supply ocpn_plugin.h in /app/include,
# also clone OpenCPN at the matching tag for headers:
# git clone --depth 1 --branch Release_5.14.0 \
#     --filter=blob:none --sparse https://github.com/OpenCPN/OpenCPN.git /tmp/ocpn-src
# git -C /tmp/ocpn-src sparse-checkout set include
# Then pass: -DOCPN_INCLUDE_DIR=/tmp/ocpn-src/include  to cmake via the manifest.
```

**Build and install the extension:**

```bash
cd opencpn_plugin/autopilot_pi
flatpak-builder --user --install --force-clean \
    build-dir flatpak/org.opencpn.OpenCPN.Plugin.autopilot.yaml
```

**Verify:**

```bash
flatpak info org.opencpn.OpenCPN.Plugin.autopilot
# Then launch OpenCPN → Plugin Manager → "AutoPilot" should appear
```

> **Note:** The exact extension mount point (`/app/extensions/autopilot/lib/opencpn/`)
> must match what the OpenCPN Flatpak manifest declares as an extension point.
> If OpenCPN doesn't list autopilot as an extension point, the plugin .so can
> also be placed in `~/.var/app/org.opencpn.OpenCPN/data/opencpn/plugins/`
> (OpenCPN's user-writable plugin directory, no Flatpak extension needed).

## Phase 6 — on-device verification

With the OrangePi connected to `SoberPilot` and the controller powered:

1. Load the plugin in OpenCPN → enable it in Plugin Manager.
2. Click the "AP" toolbar button → panel appears.
3. Confirm the connection indicator turns green within ~10 seconds (controller
   broadcasts every 1 s; we timeout after 10 s with no packet).
4. Verify displayed heading/bearing/mode match the physical display unit.
5. Press port/starboard adjust buttons; confirm both the panel and physical
   display units show the updated desired heading.
6. Set a route in OpenCPN, activate a leg, click "Navigate to active
   waypoint"; confirm the controller receives the waypoint and switches to
   waypoint-navigate mode (physical display shows mode 2 and new bearing).
