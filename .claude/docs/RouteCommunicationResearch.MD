# Route Communication: OpenCPN ⇄ Garmin GPSMAP 276c ⇄ AutoPilot

Research and design notes for exchanging routes between OpenCPN and a Garmin
GPSMAP 276c, and for having the AutoPilot follow a route the Garmin is
navigating. All communication is **indirect**, via the controller and the
existing Wi-Fi/UDP channel.

> Status: research / design proposal. Nothing here is implemented yet.

---

## 0. The physical picture (what already exists)

The Garmin 276c is wired into the **controller** through its data/power cable:
the Garmin gets power, and its serial data channels are connected to the
controller. In [`Arduino/controller/garmin.ino`](../../Arduino/controller/garmin.ino)
the controller already opens **two UARTs at 4800 baud** (`Serial1Port` on A0/A1,
`Serial2Port` on A2/A3) with TX↔RX swapped, and the comment confirms the
controller's TX is physically connected to the Garmin's RX. So the hardware to
both **read from and write to** the Garmin already exists — but `check_garmin()`
currently only *reads and logs* lines; it never writes.

The rest of the network is unchanged from the project baseline:

```
OpenCPN (autopilot_pi)  ⇄  UDP/Wi-Fi  ⇄  Controller  ⇄  Serial 4800  ⇄  Garmin 276c
        plugin             8888 bcast /        (AP, runs PID/nav)        (NMEA 0183)
                           8889 unicast
```

- Controller is the Wi-Fi AP; **broadcasts `~APDAT` on UDP 8888**, **receives
  `~APCMD` on 8889**, framing `~…$`.
- The `autopilot_pi` plugin is already a UDP client on this network and already
  has the full OpenCPN route/waypoint API available (it uses
  `GetActiveWaypointGUID()` / `GetSingleWaypoint()` today for "Send WP").

---

## 1. NMEA 0183 — the sentences that matter

### Route transfer: WPL + RTE

Routes move as a pair of sentence types:

**WPL — Waypoint Location** (one per waypoint; carries position + name):
```
$GPWPL,4807.038,N,01131.000,E,WPT001*XX
        │ lat ddmm.mmm │ lon dddmm.mmm │ name │ checksum
```

**RTE — Route** (lists waypoint *names*, in order; split across multiple
sentences because each NMEA line is ≤ 80 chars):
```
$GPRTE,2,1,c,ROUTE01,WPT001,WPT002,WPT003,WPT004*XX
        │ │ │   │        └ ordered waypoint identifiers …
        │ │ │   └ route name
        │ │ └ c = complete list / w = working (remaining legs)
        │ └ this sentence number
        └ total RTE sentences for this route
```

- Checksum = XOR of every char between `$` and `*`, two hex digits; lines end
  with `\r\n`.
- The names in RTE **must exactly match** the names in the WPL sentences — that
  is how a receiver stitches positions to the route. The route is sent as: all
  WPL sentences, then the RTE sentence(s).
- This is the universal, no-driver way routes cross between marine devices, and
  exactly what OpenCPN's built-in **Send to GPS / Receive from GPS** emits when
  "Garmin host mode" is off. **There is no handshake/ACK in plain NMEA.**

### Active-leg / autopilot sentences (used in §3)

| Sentence | Key fields | Notes |
|---|---|---|
| **RMB** | status `A`/`V`, XTE + steer L/R, origin & **dest waypoint ID**, **dest lat/lon**, range & bearing to dest, **arrival flag** | Emitted whenever a destination waypoint is active. Self-contained (gives dest lat/lon directly). **Always transmitted** by the 276c. |
| **XTE** | status, **cross-track-error magnitude (nm)**, **direction to steer L/R** | Always transmitted. |
| **BOD** | **bearing origin→destination** (the leg's intended track), dest & origin IDs | Set at leg activation, not re-derived continuously. |
| **BWC** | UTC, **waypoint lat/lon**, bearing (T/M), distance, waypoint ID | Always transmitted; alternative source of dest position/bearing. |
| **APB** | status, XTE + steer, arrival/perpendicular flags, bearing O→D, **dest waypoint ID**, bearing-to-dest, **heading to steer** | *The* dedicated autopilot sentence, but references dest by **ID, not lat/lon**, and is **not** in the 276c's always-on set (added in later firmware — not guaranteed). |

---

## 2. The Garmin GPSMAP 276c — what it can do

Two serial ports (COM1/COM2), several selectable "Serial Data Formats." The two
relevant ones:

| Mode | Direction | Routes/Waypoints | Notes |
|---|---|---|---|
| **NMEA In/NMEA Out** | both | **WPL & RTE switchable output**; **accepts WPL/RTE input** to add/overwrite waypoints & routes | NMEA v3.01, default **4800 baud** (matches the firmware today), output every 2 s (or 1 s "Fast"). Always sends RMC/GGA/GLL/BWC/VTG/XTE/RMB. |
| **GARMIN (proprietary)** | both | Full waypoints + routes + **tracks**, with ACK/NAK reliability | Binary "Garmin Device Interface" used by MapSource, **9600 baud**. While in this mode the unit is *not* streaming live NMEA position. |

Capacity: **3,000 waypoints, 50 routes, ~1,200 waypoints/route** — far more than
needed.

**Two asymmetries to remember:**

1. **OpenCPN → Garmin (load a route):** clean. In NMEA In/Out the 276c ingests
   WPL+RTE and creates the waypoints + route in memory.
2. **Garmin → OpenCPN (pull a route):** over NMEA the 276c emits WPL/RTE only for
   the **route it is actively navigating** (cyclically), not its whole library
   or all stored waypoints. That's usually exactly what you want underway. Full
   library/all-waypoints export would require GARMIN host mode.

**Name caveat:** NMEA waypoint IDs are short (older Garmins enforce 6 chars; the
276c is more generous but truncates). Whatever name is sent in WPL must survive
truncation and still match the RTE list. Generate short, unique IDs.

**One-time Garmin setup:** set the wired COM port → **Serial Data Format =
NMEA In/NMEA Out**, **4800 baud**, and enable **GPWPL** and **GPRTE** in the
sentence list.

---

## 3. Recommended transport: a thin NMEA pipe over UDP

Keep route logic in the **plugin** (full OpenCPN route API + real debugger) and
make the **controller a dumb, type-filtered relay**. This mirrors the project's
existing split (authority/steering in the controller; presentation/commands in
the plugin) and keeps fiddly NMEA string assembly/checksums out of ESP32
firmware.

### New UDP messages (same `~…$` framing)

| Message | Dir | Purpose | Controller behavior |
|---|---|---|---|
| `~APTX,<nmea sentence, no CRLF>$` | plugin → controller → Garmin | push WPL/RTE to plotter | strip frame, append `\r\n`, write to Garmin UART |
| `~APRX,<raw nmea line>$` | controller → plugin (bcast on 8888) | relay Garmin sentences of interest | forward **only** WPL/RTE (and optionally RMB/XTE/BWC) so UDP isn't flooded by 1–2 Hz RMC/GGA |

The controller never parses a route — it shuttles framed NMEA lines in/out and
gets a free Garmin-NMEA debug tap in the plugin for free.

### Firmware touch points
- [`garmin.ino`](../../Arduino/controller/garmin.ino): keep the read loop; when a
  complete line is `WPL`/`RTE` (etc.), publish it as `~APRX,…$` on 8888.
- [`subscribe.ino`](../../Arduino/controller/subscribe.ino): on `~APTX,…$`, write
  `line + "\r\n"` to the correct `SerialNPort`. Mind the existing per-call read
  time budget so a route burst doesn't starve the FreeRTOS tasks — pace outbound
  lines across loop iterations.

### Phase 2 (only if needed): GARMIN host mode
If "Garmin → OpenCPN" must export *any* stored route or the full waypoint
library (not just the active route), implement the GARMIN binary protocol
(Device Interface Spec): make the relay a transparent 9600-baud byte pipe
(hex/base64-framed) and let the plugin (or OpenCPN's own "Garmin host mode")
speak it. Materially more work, and the unit drops live NMEA position while in
that mode — reach for it only if the NMEA path proves insufficient.

---

## 4. Route exchange — directions and triggers

Both directions ride the §3 relay. They differ mainly in *what triggers them*.

### OpenCPN → Garmin ("push a route to the plotter")
- Plugin serializes the chosen route into `WPL`+`RTE` (short unique names,
  correct checksums) → `~APTX` datagrams → controller writes to the Garmin UART
  → the 276c (NMEA In/Out) stores the waypoints + route.
- **Trigger: deliberate operator action — NOT automatic.** OpenCPN cannot know
  which route you want on the Garmin or when, so add a **"Send Route → Garmin"**
  button in the plugin (sibling to the existing "Send WP"). Build/edit a route,
  then click send. (Optionally also auto-send a route on "Activate" in OpenCPN —
  but keep that opt-in, not default.)

### Garmin → OpenCPN ("pull the active route in")
- **Trigger: automatic, driven by the Garmin.** When you start navigating, the
  276c cyclically emits WPL/RTE for the **active route**. Controller relays as
  `~APRX`; plugin reassembles and creates/activates the matching route so the
  chart shows the same track.
- Caveat: NMEA gives the **active** route only, not the whole library.

> **The Garmin pushes its active route to OpenCPN by itself; OpenCPN pushes to
> the Garmin only when you press the button.**

---

## 5. AutoPilot follows the Garmin's navigation

**Core idea:** the Garmin owns route sequencing; the AutoPilot just mirrors the
*current active leg*, re-read every 1–2 s. **Parse this in the controller
firmware** (not the plugin) so it works even with OpenCPN/OrangePi off — start
navigating on the Garmin and the AutoPilot follows. It also reuses the existing
mode-2 waypoint machinery (`setWaypoint`/`setMode`), and the active waypoint
still flows out via `~APDAT` so TFT + plugin panel reflect it.

> This section covers the **Garmin as nav source**. Navigation can equally be
> started in **OpenCPN** (which feeds the controller via `~APCMD` `setWaypoint`),
> and both can be active at once. How the controller picks one source, fails over,
> and keeps working with only one device is covered in **§7**.

### Option 1 — Track `RMB` (the foundation)

`RMB` is always transmitted while a destination is active and is self-contained:

- **Status `A`/`V`** = navigation on/off → engage/disengage trigger.
- **Destination lat/lon** → feed straight into `setWaypoint()`; auto-updates as
  the Garmin advances legs.
- **Arrival flag** → waypoint reached / end of route.

Logic: on `V→A`, set waypoint + engage nav (mode 2). While `A`, keep updating the
waypoint. On `A→V`, disengage (compass-hold or off). The AutoPilot keeps
computing its own bearing from its own GPS+compass — you only hand it *where* to
go.

**Engagement guard:** put this behind an **"armed Follow-Garmin" toggle** so the
wheel doesn't move the instant you press Navigate.
- *Armed (recommended):* RMB `A` auto-engages.
- *Disarmed:* RMB just *populates* the waypoint (`waypoint_set` + wp_lat/lon shown
  on screen); operator presses Enable to actually steer.

This preserves the project's existing "operator presses Enable" safety pattern.

### Option 3 — add cross-track steering (the refinement)

Option 1 steers **at** the waypoint, so current/leeway pushes you off the
intended line and you arrive on a curve. Option 3 steers **the line between
waypoints**, using two always-available sentences:

- **`XTE`** — cross-track-error magnitude (nm) + direction to steer (L/R) back to
  the line.
- **`BOD`** — bearing origin→destination = the leg's intended track course.

**Steering math** (in [`pid.ino`](../../Arduino/controller/pid.ino)): today, in
mode 2, `desired_heading = bearing_to_waypoint`. With cross-track steering, aim
at the *line* instead:

```
desired_heading = leg_course  ±  clamp(Kxt × XTE,  ±max_correction)
```

- `leg_course` = the rhumb line you should be on (from `BOD`, or computed from
  origin→dest). Refresh it when `RMB`'s destination ID changes (new leg).
- `Kxt × XTE` pulls you back proportionally to how far off the line you are; sign
  from XTE's L/R field; clamp the correction (e.g. ≤ 30–45°) so a large offset
  doesn't command a U-turn.
- As `XTE → 0` you settle onto and hold the line rather than crabbing toward the
  mark.

Practical notes:
- `BOD` is set at leg activation and not re-derived continuously — fine as the
  leg's fixed intended course; just refresh on new leg.
- Near the waypoint, blend back toward plain bearing-to-waypoint (or honor RMB's
  arrival flag) so tiny XTE values don't cause nervous steering at the mark.
- Keep `Kxt` and the clamp in the existing PID tuning workflow ([`pid/`](../../pid)).

Option 3 is a **drop-in upgrade of the `desired_heading` term** within Option 1's
framework — same RMB-driven engage/disengage, smarter target heading.

### Option 2 — `APB` (optional bonus)

`APB` provides a precomputed **"heading to steer"** and bearing-to-dest. But it's
not guaranteed on the 276c (later-firmware only), references the dest by **ID,
not lat/lon** (needs WPL to resolve position), and the AutoPilot already derives
steering from bearing + compass. Treat APB as an opportunistic bonus layered on
Option 1, not the foundation.

---

## 6. Preventing duplicate routes (the round-trip problem)

If you create a route in OpenCPN, **Send Route → Garmin**, then press Navigate,
the Garmin sends that same route back — and a naive ingest creates a **duplicate**
in OpenCPN. Fix: make the inbound path **idempotent**.

### Distinguish the two intents

The Garmin→OpenCPN stream carries two different intents:
- **(a)** "This route already lives in OpenCPN; I'm just navigating it now" → do
  **not** create; mark the existing route active.
- **(b)** "I built this route on the Garmin itself" → genuinely new; import it.

Duplicates happen only because the naive design treats everything as (b). Add a
match step and (a) stops duplicating.

### Match inbound → existing (two keys, used together)

1. **Name tag (primary).** When the plugin sends a route, give it a
   **deterministic short identifier** as the RTE route name (e.g. `OCPN01`) — the
   Garmin truncates, so don't rely on the pretty name surviving. Keep a map
   `shortID → OpenCPN route GUID` in the plugin. On inbound, read the RTE name;
   if it's in the map → same route → **activate the existing GUID, suppress
   creation.**
2. **Geometry fingerprint (fallback/safety net).** Fingerprint by waypoint count
   + sequence of positions; compare to existing routes. **Tolerance-based**, not
   exact: WPL encodes lat/lon as `ddmm.mmm` (~1.8 m / ~0.001′ resolution), so a
   returned waypoint is never bit-identical — match within a small epsilon
   (a few meters / ~0.01 nm).

If **either** key matches → adopt existing. If **neither** → genuinely new (b) →
import.

### Cheaper second layer — reuse the project's own pattern

Apply the existing **optimistic-UI suppression** idiom (`localCommandTime` / the
plugin's 2 s field suppression): when the plugin sends a route, record its
signature + timestamp; suppress creation for that signature on the immediate echo
when you press Navigate. The match logic above is the durable mechanism; this
guarantees the obvious "sent it 5 s ago, here it bounces back" case never reaches
the create path.

### The cleaner architecture this implies

For routes that **originated in OpenCPN, you don't need to ingest WPL/RTE at
all** — OpenCPN already has the geometry. All you need from the Garmin is *which
route is now active*, and **`RMB` already tells you that** (its destination
identifies the active leg). So:

- **Originated in OpenCPN:** on `RMB` active, find the local route whose waypoint
  matches RMB's destination → **activate your own copy**. No ingest, no duplicate
  possible.
- **Originated on the Garmin** (OpenCPN has never seen it): name/geometry match
  fails → *then* build it from the inbound WPL/RTE.

> **Rule: `RMB` activates an existing route; WPL/RTE ingest is reserved for
> routes OpenCPN doesn't already have.** This eliminates duplicates by
> construction and only does the heavier import when there's genuinely new
> information.

### Gotchas to bake in
- **Name length/charset:** send a stable short ID; keep the human name in the
  local map — don't expect OpenCPN's full route name to round-trip.
- **Position tolerance:** always match with an epsilon, never exact equality
  (`ddmm.mmm` rounding).
- **Edited-after-send:** if you edit the route in OpenCPN after sending, the
  fingerprint diverges and it looks "new." Treat the OpenCPN copy as
  authoritative and offer a re-send (overwrite on the Garmin), or warn.

---

## 7. Two nav masters — mirroring & source arbitration

So far the doc has assumed the **Garmin** starts navigation. But navigation can
be initiated from **either head**:

- **OpenCPN:** drop/select a waypoint or activate a route, then **Follow**.
- **Garmin:** select a route/waypoint, then **Navigate**.

The goal is that **whichever you start first sets the other**, the two screens
mirror the same active route, and the AutoPilot steers the same leg regardless of
origin — *and* if one device fails, the AutoPilot keeps working on the survivor.

### 7.1 The mirror is asymmetric

| Direction | Achievable? | How |
|---|---|---|
| **Garmin → OpenCPN** (Navigate on Garmin, OpenCPN follows) | **Yes, cleanly** | Already designed (§4/§5/§6): Garmin emits RMB + WPL/RTE → plugin activates the matching OpenCPN route. OpenCPN mirrors automatically. |
| **OpenCPN → Garmin** (Follow in OpenCPN, Garmin navigates) | **Partial** | The route can be *pushed* for display (§4), but plain NMEA has no "start navigating" command — see below. |

**Why OpenCPN → Garmin is hard.** Plain NMEA 0183 has **no sentence that
remotely commands the Garmin to *begin* navigating.** NMEA In on the 276c will
ingest WPL/RTE and *store* the route (§4), but the operator must physically press
**Navigate** on the unit to make a leg active — there is no remote
"activate route / GOTO" in the always-on NMEA set. Two ways to close the gap:

1. **GARMIN proprietary host mode (Phase 2).** The Device Interface protocol can
   set an active route / start a GOTO on some Garmin units. Would need
   confirmation the 276c honors a *remote* activate, and the unit **drops live
   NMEA while in host mode** (mode conflict): you'd activate in host mode, then
   switch COM1 back to NMEA In/Out to regain the live RMB stream — racy and
   clunky. Defer unless truly needed.
2. **Don't require the Garmin to navigate at all (recommended).** If you start in
   OpenCPN, have the **AutoPilot follow OpenCPN directly** — the plugin already
   knows the active waypoint (`GetActiveWaypointGUID()`) and already has a path to
   the controller (`~APCMD` `setWaypoint`). Still *push* the route to the Garmin
   for situational display, but accept its screen won't read "Navigating" unless
   you press Navigate on the unit.

**Bottom line:** the *functional* goal — AutoPilot steers the right leg no matter
which head you start, and survives either head failing — is **fully achievable**.
The only piece NMEA can't deliver is the *cosmetic* one (auto-lighting the
Garmin's own Navigate screen from OpenCPN), which needs host mode or a button
press on the unit.

### 7.2 Two independent nav sources into the controller

The controller can learn "we are navigating leg X" from two places:

1. **Garmin source** — RMB/XTE/BOD parsed off the UART (§5). Works with the
   OrangePi / OpenCPN **off**.
2. **OpenCPN source** — the plugin's active waypoint, delivered over the existing
   `~APCMD` `setWaypoint` path. Works with the Garmin **off** (or not navigating).

When **both** are active, the controller is told the active waypoint *twice*.
Even navigating the *same* route the two streams won't be bit-identical, and —
critically — **won't necessarily advance to the next leg at the same instant**
(different arrival-circle radii / perpendicular-pass logic). Blending two
slightly out-of-phase sources causes a brief wrong-target glitch at every
waypoint. **So: pick exactly one source to steer from at a time** — never
average, never merge.

### 7.3 Source arbitration (in the controller)

Arbitration lives in the **controller** — it is the steering authority and the
only place both sources converge. Model an explicit nav-source selector with
three states: **GARMIN**, **OPENCPN**, **NONE**.

- **Liveness per source.** A source is "live" only if it is both *active*
  (Garmin: RMB status `A`; OpenCPN: plugin reports an active WP) **and** *fresh*
  (updated within a timeout — e.g. RMB within ~6 s; OpenCPN WP within ~6 s,
  reusing the existing `~APDAT`/heartbeat cadence). A source that goes stale is
  treated as **failed**.
- **One live → follow it.** No arbitration needed. This *is* the single-device
  case ("function with only OpenCPN, or only the Garmin").
- **Both live → follow the primary only.** Steer from the primary; ignore the
  other *for steering* but keep watching its liveness for failover. (Which source
  is primary is discussed just below.)
- **Automatic failover.** If the active source goes stale/inactive, fall over to
  the other if it's live: Garmin dies mid-route → continue on OpenCPN's waypoint;
  OpenCPN/OrangePi dies → continue on the Garmin's RMB. This is the "one has
  failed, keep going" requirement.
- **Hysteresis / hand-back.** Don't thrash at the boundary. When a higher-priority
  source returns, switch back only after it has been steadily live for a few
  seconds, and **log every source switch**.

#### Which source should be primary?

**Default: the Garmin.** Three reasons:

1. **Lowest dependency / survives the most failure modes.** The controller parses
   the Garmin's RMB/XTE/BOD directly off the UART, so that path keeps steering
   even if Wi-Fi, the OrangePi, or OpenCPN drops. The OpenCPN source depends on a
   healthy `~APCMD` link with more hops — make the more fragile source the
   *fallback*, not the primary.
2. **It's the steering authority's native input.** The Garmin source lives
   entirely inside the controller; nothing external has to be healthy for it to
   work.
3. **Cross-track quality.** The Garmin emits `XTE`/`BOD` on the wire — exactly
   what Option 3 line-steering consumes — so driving from the Garmin keeps the leg
   definition and the cross-track data coming from one consistent source.

**But make it operator-selectable** (a param/toggle), because OpenCPN-as-primary
is a legitimate preference: OpenCPN is where routes are built/edited and has the
richer chart picture, so for a planning-in-OpenCPN / Garmin-for-display workflow,
preferring OpenCPN avoids a needless failover dance. Either way **failover is
automatic** — whichever source isn't primary becomes hot standby and takes over
within the liveness timeout if the primary goes stale.

> **Open question to confirm before implementation:** can the plugin report
> OpenCPN's *active* waypoint at roughly the same ~1 s cadence the controller
> expects for liveness? If the OpenCPN-source update rate is slower or laggier
> than RMB, that further reinforces **Garmin as the default primary.**

### 7.4 Agreement check (sanity, not blending)

While both are live, compare their destinations: same lat/lon within the §6
epsilon ⇒ they **agree**, follow the primary silently. Destinations **differ**
(different waypoint / different route) ⇒ genuine **conflict** — the two heads are
navigating different things. Do **not** average; follow the primary but raise a
flag (telemetry/console) so the operator notices the heads disagree.

> Re the user's Option-1 note: if both navigate the *same* route and you only
> steer **to the mark** (Option 1), the small phase difference at leg-advance is
> largely masked — the destinations stay nearly identical until one head advances.
> But you still must **pick one source** so the advance instant is unambiguous;
> arbitration is what guarantees that, and it becomes essential the moment you
> add cross-track steering (Option 3), where XTE/BOD must come from one consistent
> leg definition.

### 7.5 Telemetry

Surface the current nav source **and** the standby source's status in `~APDAT`
so the TFT and plugin panel can show, e.g., `Following: GARMIN (OpenCPN live,
standby)` or `Following: OPENCPN (Garmin offline)`. This makes failover visible
and the whole arbitration debuggable from either screen.

### 7.6 Where the mirroring logic lives

| Concern | Lives in |
|---|---|
| Garmin → OpenCPN mirror (activate matching route on RMB) | **plugin** (§4/§6) |
| OpenCPN → Garmin route push (display) | **plugin** (§4) |
| OpenCPN → Garmin *remote-activate* | Phase-2 host mode, or a manual Navigate press |
| Source arbitration, liveness, failover, agreement check | **controller** (§7.3) |

---

## 8. Putting it all together

**Either head can start navigation; the other mirrors as far as it can; the
controller picks one source to steer and fails over to the survivor.**

**Scenario A — start on the Garmin (the cleanest mirror):**
1. Build a route in OpenCPN → press **Send Route → Garmin** → it lands in the
   276c (`WPL`/`RTE` via `~APTX`). *(operator-triggered, optional pre-step)*
2. Press **Navigate** on the Garmin. It emits the active route's `WPL`/`RTE` →
   relayed as `~APRX` → OpenCPN **recognizes its own route and just activates it**
   (no duplicate; §6) — **OpenCPN now mirrors the Garmin.** *(Garmin-triggered)*
3. Simultaneously the Garmin emits `RMB` (+`XTE`/`BOD`). With **Follow-Garmin
   armed**, the controller selects the **Garmin** nav source (§7.3) and steers the
   active leg — Option 1 to the mark, **Option 3 to the line.** *(Garmin-triggered)*
4. The Garmin auto-advances at each waypoint; `RMB`'s destination changes; the
   AutoPilot follows automatically. At route end (`RMB`→`V`) it disengages.

**Scenario B — start in OpenCPN:**
1. Activate/Follow a route in OpenCPN. The plugin pushes it to the Garmin for
   display (`~APTX`) and reports the active waypoint to the controller via
   `~APCMD` `setWaypoint`. *(operator-triggered)*
2. The controller selects the **OpenCPN** nav source (§7.3) and steers the active
   leg. The Garmin shows the route but won't read "Navigating" until you press
   Navigate on the unit (NMEA limitation; §7.1) — press it and the controller
   simply sees both sources agree and stays on its primary.

**Shared, both scenarios:**
- If **both** heads are navigating, the controller follows **one** source (primary,
  default Garmin) and treats the other as hot standby; mismatched destinations are
  flagged, never blended (§7.3–7.4).
- If a source **fails** (Garmin unplugged, or OrangePi/Wi-Fi down), the controller
  **fails over** to the survivor and keeps steering (§7.3).
- Throughout, the active waypoint **and the current nav source** flow into the
  existing `~APDAT` telemetry, so the TFT display and plugin panel show the same
  target and who's driving it (§7.5).

### UDP message map (extends the existing `~…$` protocol)

| Message | Dir | Purpose | Trigger |
|---|---|---|---|
| `~APTX,<nmea>$` | plugin → controller → Garmin | push WPL/RTE to plotter | operator "Send Route" |
| `~APRX,<nmea>$` | controller → plugin (bcast) | relay Garmin WPL/RTE (optionally RMB/XTE) | Garmin navigating |
| *(internal)* RMB/XTE/BOD parse | in controller | Garmin nav source: engage + steer active leg | Garmin navigating + armed |
| `~APCMD,…setWaypoint$` | plugin → controller (existing) | OpenCPN nav source: active waypoint to steer | OpenCPN Following |
| *(internal)* source arbitration | in controller | choose Garmin/OpenCPN, fail over (§7.3) | ≥1 source live |
| `~APDAT,…$` | controller → all (existing) | active waypoint **+ nav source** shown everywhere | every tick |

Both features share one relay and one principle — **the controller reads the
Garmin's NMEA; routes go up to OpenCPN for the chart, and the live leg goes into
the steering loop** — with the only operator-initiated step being the deliberate
"push a route to the Garmin," and de-duplication handled by recognizing routes
OpenCPN already owns.

### Build order (suggested)
1. Relay plumbing: `~APTX`/`~APRX` in firmware + plugin (test with raw NMEA).
2. OpenCPN → Garmin push (WPL/RTE generation + "Send Route" button).
3. Follow-Garmin Option 1 (RMB parse + armed engage in controller).
4. De-duplication (RMB-activates-existing; ingest only unknown routes).
5. OpenCPN nav source (controller treats `~APCMD` `setWaypoint` as a steering
   source with its own liveness) + the **arbitration/failover** state machine
   (§7.3) — this is what makes single-device operation and failover real.
6. Option 3 cross-track steering (XTE/BOD in `pid.ino`).
7. (Optional) APB if firmware emits it; (Phase 2) GARMIN host mode — incl. the
   only path to OpenCPN→Garmin *remote-activate* (§7.1).

---

## 9. Testing & emulation (no real Garmin on the bench)

The 276c isn't available during development, so the Garmin side has to be
emulated. Note the emulator stands in for a **serial NMEA device**, not an IP
endpoint — it talks to the controller over the same UART the real Garmin uses
([`garmin.ino`](../../Arduino/controller/garmin.ino), 4800 baud). Three tiers,
ordered by how much of the stack they exercise; use them together.

### Tier 1 — Telnet NMEA injection (no extra hardware)

The controller already has a telnet console ([`telnet.ino`](../../Arduino/controller/telnet.ino)).
Add one command that **injects a line as if it arrived on the Garmin UART**,
feeding it into the same path `check_garmin()` uses. This drives everything
*above the wire*: the `~APRX` relay, the plugin's WPL/RTE parsing, route de-dup
(§6), and the RMB-follow + steering logic (§5). Exercises ~90% of the new
(risky) code with zero breadboard. Does **not** test the real UART
electrical/baud/framing path — but that's the least risky part. Start here.

### Tier 2 — Laptop + 3.3 V USB-serial adapter running a Python "Garmin" (primary)

Tap the controller's Garmin UART pins (A0–A3) with an FTDI/CP2102 adapter —
**3.3 V logic, not 5 V**, common ground — and run a Python script that plays the
276c:
- streams idle RMC/GGA,
- emits WPL/RTE + cycling RMB on command, advancing legs,
- **reads back** what the controller writes, so the OpenCPN→Garmin push is
  verified too.

Real serial link into the real controller, with a scriptable / assert-able /
log-replayable driver where iteration is "edit Python + rerun" instead of
reflashing. Control is native to the script — no telnet layer needed on the
emulator. This is where most of the test effort should go.

### Tier 3 — Second Arduino Nano ESP32 emulator (untethered rig)

A second Nano ESP32 on a breadboard, wired to the controller's Garmin UART
(cross TX/RX, common ground — both boards are 3.3 V, so direct wiring is safe),
emulating the 276c's serial behavior. Best when you want a **self-contained
bench rig with no laptop tethered**, or to validate the actual board-to-board
UART electrical/timing path before the real Garmin exists. Telnet-to-control
over the SoberPilot AP is reasonable here (give it a fixed IP). Build it to also
**consume and echo** a pushed route, not just inject, so it validates both
directions and the §6 round-trip de-dup.

### Canned scenarios (whichever tier)

1. **Idle** — RMC/GGA only, no active waypoint. Verifies the relay filters out
   the high-rate stream and the AutoPilot does *not* engage.
2. **Navigating a 3-waypoint route** — WPL/RTE burst, then RMB advancing per leg
   with the arrival flag, ending with RMB→`V`. Verifies route-in, RMB-follow,
   leg auto-advance, and disengage-at-end.
3. **Accept-and-echo a pushed route** — receive the WPL/RTE the controller sends
   (`~APTX`), then replay it back as if navigating it. Verifies OpenCPN→Garmin
   push *and* the de-dup path (OpenCPN must recognize its own route, not
   duplicate it).
4. **Dual-source arbitration + failover** — drive the emulated Garmin (RMB
   navigating) *and* feed an OpenCPN active waypoint (`~APCMD` `setWaypoint`) at
   the same time. Verify the controller steers from **one** source (the primary),
   doesn't blend at leg-advance, and flags a deliberate destination mismatch.
   Then **kill the primary** (stop RMB, or stop the `~APCMD` heartbeat) and verify
   it **fails over** to the survivor within the liveness timeout, and **hands
   back** with hysteresis when the primary returns (§7.3–7.4). Also run each
   source **alone** to confirm single-device operation.

**Recommendation:** develop the logic on Tier 1, use **Tier 2** as the main
emulator, and build **Tier 3** only for an untethered rig or to shake out the
physical UART link.

### COM1 / COM2 during testing

A single NMEA In/Out channel carries everything this project needs (position,
active leg, route in/out), so emulate just **one** channel to start. Keep the
second wired (it already is — `Serial2Port` on A2/A3) as a reserve for the
Phase-2 GARMIN host-mode path (9600 baud), which can run on COM2 *without*
switching COM1 out of NMEA In/Out and interrupting the live nav stream. Don't
split the live NMEA function across both ports — there's no benefit and it
complicates firmware and Garmin config. Net split, if/when host mode lands:
**COM1 = live NMEA + per-route push; COM2 = bulk host-mode transfer.**

---

## Sources

- [NMEA Revealed — sentence field definitions (WPL, RTE, RMB, APB, XTE, BOD, BWC)](https://gpsd.gitlab.io/gpsd/NMEA.html)
- [OpenCPN — Navigation Sharing / Send to GPS](https://opencpn.org/wiki/dokuwiki/doku.php?id=opencpn:manual_basic:route_manager:navigation_sharing)
- [OpenCPN — Send WPT/RTE over IP](https://opencpn.org/wiki/dokuwiki/doku.php?id=opencpn:manual_advanced:techniques:send-wpt-rte-over-ip)
- [Garmin GPSMAP 276c Owner's Manual — Advanced NMEA Output Setup (ManualsLib p.96)](https://www.manualslib.com/manual/253213/Garmin-Gpsmap-276c.html?page=96)
- [Garmin GPSMAP 276c Owner's Manual (PDF)](https://www.tramsoft.ch/downloads/garmin/gpsmap276c_owners-manual.pdf)
- [Garmin Support — Transfer user data from MapSource to GPSMAP 276c (GARMIN proprietary mode)](https://support.garmin.com/en-US/?faq=2Uzd2TK59q1f3R0A1WUro7)
- [Yacht Devices — WPL/RTE route transfer over NMEA 0183](https://www.yachtd.com/news/wpl_rte_update.html)
- [Garmin GPSMAP 276c NMEA Output discussion (APB added in later firmware)](https://forums.ybw.com/threads/garmin-gpsmap-276c-nmea-output.60058/)
