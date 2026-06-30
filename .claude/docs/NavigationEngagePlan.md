# Navigation Engagement Plan — "operator always engages"

Companion to [`RouteImplementationPlan.md`](RouteImplementationPlan.md) and
[`RouteCommunicationResearch.MD`](RouteCommunicationResearch.MD). This doc
refines the *engagement* behavior agreed 2026-06-29 and **supersedes
`RouteImplementationPlan.md` §2.4's "arming guard / armed → RMB auto-engages
nav."** Nothing auto-engages navigation anymore; the arming concept is dropped.

---

## 0. The one rule that drives the whole design

**Navigation is never auto-enabled, from any source.** A nav source (Garmin RMB
*or* OpenCPN Follow) may auto-set **Mode → 2** so the controller is *aimed* at the
waypoint, but it never calls `setNavigationEnabled(true)`. The operator presses
**Enable** when underway — every time, regardless of where the route/waypoint
came from.

**Why this is safe:** `control_task` only runs the PID → motor path when
`navigation_enabled` is true (`Arduino/controller/controller.ino:97-117`). Mode 2
with nav **off** does not move the rudder. So "auto Mode 2, manual Enable" is a
safe, deliberate state: the controller is locked onto the waypoint and waiting
for the operator's go.

---

## 1. Wire contract (freeze first — both machines depend on it)

No new "Follow" command. The Follow-vs-one-shot distinction lives in the
**cadence** of the existing lowercase `w`, not in a new opcode.

| Cmd | From → To | Meaning | Controller effect |
|---|---|---|---|
| `~APCMD,w<lat>,<lon>$` | plugin → ctrl (8889) | set waypoint — one-shot **or** heartbeat, same bytes | always `setWaypoint()`; refresh the OPENCPN-source liveness clock |
| `~APCMD,X$` | plugin → ctrl (8889) | *(optional)* Follow stopped | clear the OPENCPN source immediately instead of waiting out the timeout |
| `~APCMD,m<1\|2>$`, `~APCMD,n<0\|1>$`, `~APCMD,a<deg>$` | plugin/display → ctrl | existing manual mode / nav-enable / heading-adjust | unchanged — these are the operator's manual controls |
| `~APTX,<nmea>$` | plugin → ctrl | push a route line to the Garmin UART | unchanged (already implemented) |
| `~APRX,<nmea>$` | ctrl → all (8888) | relay of a Garmin-received `WPL/RTE/RMB/XTE/BOD` line | unchanged (already implemented) |
| APDAT `…,<nav_source>$` | ctrl → all (8888) | who's steering: `0` NONE / `1` GARMIN / `2` OPENCPN | **new, additive** (Phase B) |

### Why no capital `W`

Earlier drafts proposed `W` (Follow) vs `w` (one-shot) so the controller could
tell them apart. Unnecessary: **Follow is just `w` repeated a few times a second
— that repetition *is* the heartbeat.** The controller distinguishes a sustained
stream (Follow) from a lone packet (Set WP) by rate (§2). This also removes the
wart `RouteImplementationPlan.md` §1.2 had accepted — a one-shot `w` no longer
ever looks "live for steering."

---

## 2. Controller model — unified two-source selector

`Arduino/controller/navsource.ino` is rewritten from the Garmin-only/armed logic
into a **two-source selector**. Delete `follow_garmin_armed` and the
auto-`setNavigationEnabled(true)` path.

Track **GARMIN** and **OPENCPN**, each `{dest_lat, dest_lon, live, last_update_ms}`:

- **GARMIN**: `live` set on RMB status `A`, cleared on RMB `V`, refreshed each RMB.
- **OPENCPN**: promoted to `live` only on a **sustained `w` heartbeat** — e.g. a
  second `w` within ~3 s of the first. A continuous Follow stream crosses that
  immediately; a lone **Set WP** click never does (it sets the waypoint but does
  not promote → no Mode 2). Cleared on ~6 s of `w` silence or on `X`.

**Selector** (evaluated each `navsource_tick()`): GARMIN if live, else OPENCPN if
live, else NONE. *(Garmin is favored — operator requirement.)*

- selected ≠ NONE → `setWaypoint(selected.dest)` + `setMode(2)`.
- selected == NONE → if mode == 2, drop to `setMode(1)` (compass-hold idle).
- **Never** calls `setNavigationEnabled()`.

Failover is automatic: kill the Garmin → it goes stale (~6 s) → selector hands off
to OpenCPN; kill OpenCPN → hands off to Garmin. Either head can fail and the other
keeps us navigating.

Tunables to settle on the bench: heartbeat-promote window (~3 s / 2 samples),
source timeout (~6 s), any hand-back hysteresis.

---

## 3. How each operator scenario plays out

All scenarios end with the operator pressing **Enable** when ready under way.

**1 — Route on OpenCPN, Garmin in sync.** Activate the route in OpenCPN → click
**Send Rte** → plugin loads the route on the Garmin (`~APTX` WPL/RTE) **and**
auto-checks Follow → plugin streams `w` for the active leg → OPENCPN promoted →
**Mode 2, nav off**. If the operator later presses **Navigate** on the Garmin, its
RMB arrives → selector switches to **GARMIN** (favored).

**2 — Route on OpenCPN, Garmin not involved.** Activate the route + check
**Follow** → plugin streams `w` → **Mode 2, nav off**. A plain **Set WP** with
Follow *off* sends a single `w` → waypoint set, **Mode unchanged**.

**3 — Route on the Garmin.** Operator presses **Navigate** on the Garmin → RMB →
GARMIN promoted → **Mode 2, nav off**. The controller relays the Garmin's WPL/RTE/
RMB as `~APRX`; the plugin ingests them, **de-dupes** (is this a route we sent?),
and either activates the existing local route or adds + activates a new one so
OpenCPN displays it.

**Dual-source.** Both heads may feed at once. Garmin wins; loss of either fails
over to the survivor. The design degrades gracefully — as long as one source is OK
we can navigate.

### The Garmin does not auto-navigate on route upload

A standard NMEA route upload (`WPL` + `RTE`) only stores the route in the Garmin's
catalog. The Garmin does not emit `RMB` or advance legs until the operator selects
the route and presses **Navigate** on the unit. So **Send Rte loads the route for
display/backup; it does not start Garmin navigation** — consistent with the
"operator always engages" rule, and why Scenario 1 gets Mode 2 from OpenCPN's
heartbeat rather than from the Garmin. (Exact behavior is 276c-specific; confirm
with the Python emulator's read-back and the real unit.)

---

## 4. File changes by machine

### Mac — controller firmware (build/flash over USB serial)

- `navsource.ino` — rewrite as the §2 two-source selector; delete
  `follow_garmin_armed` and the auto-`setNavigationEnabled`. Add OPENCPN
  source update/clear hooks + liveness/selection in `navsource_tick()`.
- `subscribe.ino` — route `w` (and optional `X`) into navsource instead of
  calling `setWaypoint()` directly, so the selector owns `setWaypoint`/`setMode`.
  `~APTX` intake already done.
- `garmin.ino` — RMB already feeds navsource; just drop the auto-enable
  expectation.
- `telnet.ino` — remove/repurpose the `f` arm command; optional `s` to print the
  current source for debugging.
- **(Phase B)** `publish.ino` + `AutoPilot.{h,cpp}` **and** `display/AutoPilot.cpp`
  — append `nav_source` to APDAT as one atomic 3-parser bump (hard rule: all three
  positional parsers change together; append at the end).

### Navigator — OpenCPN plugin (build via flatpak, test on SoberPilot Wi-Fi)

- `AutoPilotPanel.cpp` —
  - **Follow checked** ⇒ start a `wxTimer` that re-sends `w` for the current
    active target every ~1–2 s (a dependable heartbeat — do **not** rely on
    `SetActiveLegInfo`'s native cadence, which may only fire on leg change).
  - **Set WP** (Follow off) ⇒ single `w`.
  - **Unchecking Follow** / active leg cleared ⇒ stop the timer (optionally send `X`).
  - **Send Rte** ⇒ `SendRoute` **and** auto-check Follow (start the heartbeat).
- `AutoPilotLink.{h,cpp}` — optional `SendStopFollow` (`X`); **(Phase B)** parse
  `nav_source`; **(Phase C)** `FlushInboundRoute` → de-dup + `ActivateRoutePI` /
  `AddPlugInRoute` (the §3.3 activation spike is already resolved in-code).
- **(Phase B)** panel shows "Following: GARMIN / OPENCPN."

---

## 5. Phasing & the Mac/Navigator split

- **Phase A — engagement behavior** (delivers all three scenarios correctly).
  Freeze the §1 contract, then **Mac** (navsource selector) and **Nav** (plugin
  Follow / Send-Rte wiring + heartbeat timer) proceed **in parallel**. The Python
  emulator can exercise controller arbitration without the plugin.
- **Phase B — `nav_source` telemetry + panel "who's steering"** — one atomic APDAT
  bump across controller, display, and plugin.
- **Phase C — inbound Garmin route de-dup/activate** (Nav-only) — completes
  Scenario 3's OpenCPN-side route display.

Phase A is the real answer to "stop auto-navigating"; B and C complete the picture.

---

## 6. Open tunables to settle on the bench

1. Heartbeat-promote window / sample count (~3 s, ≥2 `w`).
2. Source liveness timeout (~6 s each) and any GARMIN→OPENCPN hand-back hysteresis.
3. Plugin heartbeat cadence (~1–2 s) vs. the controller's promote/timeout windows
   (cadence must be comfortably shorter than the timeout).
4. `nav_source` field encoding (one int: 0/1/2) — decide before Phase B.
