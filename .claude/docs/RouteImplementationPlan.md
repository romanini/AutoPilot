# Route Communication — Implementation Plan

Companion to [`RouteCommunicationResearch.MD`](RouteCommunicationResearch.MD).
That doc is the *why* and the design rationale (read it first); this doc is the
*how, in what order, and on which machine*. Section references like "§7.3" point
into the research doc.

> Scope of this plan: the three asks —
> 1. **Bidirectional route sync** OpenCPN ⇄ Garmin, with de-duplication.
> 2. **Start navigation from either head** (OpenCPN *or* Garmin), both able to
>    navigate, controller arbitrates and fails over.
> 3. A **Python Garmin emulator** running on the MacBook to test all of it.

---

## 0. Development is split across two machines — read this first

There are two Claude working environments, and the split is deliberate:

| Machine | Owns | Toolchain present | Builds / tests |
|---|---|---|---|
| **Mac** (this repo checkout) | `Arduino/controller/*` firmware **and** the new Python Garmin emulator + test harness | `arduino-cli`, USB-serial to the ESP32, Python, `monitor/` | compile + flash controller; run emulator over an FTDI/USB-serial tap; Tier-1 telnet injection |
| **Navigator** (OrangePi, OpenCPN box) | `opencpn_plugin/autopilot_pi/*` | `flatpak-builder`, a real OpenCPN with the route API + chart picture | build + load the plugin; exercise route create/activate; live `~APRX`/`~APDAT` on the SoberPilot Wi-Fi |

**Why this matters for the plan:** the controller and the plugin talk only
through the UDP protocol, and the emulator/plugin/controller all independently
generate or parse the *same* NMEA. So the **one thing that must be agreed before
either side writes code is the wire contract in §1.** Both Claude instances
implement to §1 as the single source of truth. After that, the two tracks
(controller+emulator on Mac, plugin on Navigator) proceed largely in parallel,
with a small number of cross-track dependencies called out in §6.

A nice property falls out of this split: **the three implementations cross-check
each other.** The emulator's NMEA output flows through the controller relay into
the plugin's parser; the plugin's pushed route flows back through the controller
into the emulator's parser. If all three agree on §1, integration "just works";
if one drifts, a round-trip test fails loudly.

---

## 1. The wire contract (freeze this first — shared by all three components)

All framing stays `~…$` exactly as today. Two new messages plus one additive
APDAT change and one new command.

### 1.1 New UDP messages (extend the existing protocol)

| Message | Direction | Carrier | Meaning |
|---|---|---|---|
| `~APTX,<nmea sentence, no CRLF>$` | plugin → controller (8889) | unicast | "write this NMEA line to the Garmin UART." Controller strips the frame, appends `\r\n`, writes to `Serial1Port`. |
| `~APRX,<raw nmea line>$` | controller → all (8888) | broadcast | "I received this line from the Garmin." Controller relays **only** `WPL,RTE,RMB,XTE,BOD` (drop high-rate `RMC/GGA/GLL/VTG` so UDP isn't flooded). |

### 1.2 OpenCPN as a *steering source* needs a heartbeat (new command)

Today the plugin's `SendWaypoint()` emits `~APCMD,w<lat>,<lon>$` — a **one-shot**
"Set WP" (manual). Arbitration (§7.3 of research) needs OpenCPN to be a *live*
source the controller can time out. So distinguish:

| Command | Meaning | Cadence |
|---|---|---|
| `~APCMD,w<lat>,<lon>$` | **manual** Set-WP, one-shot (unchanged) | on button press |
| `~APCMD,W<lat>,<lon>$` | **OpenCPN active-nav leg** heartbeat (new; capital W) | repeated ~1–2 s while OpenCPN is Following |
| `~APCMD,X$` | **OpenCPN nav stopped** (new) | once when Following ends |

Capital `W` updates the waypoint *and* refreshes the OpenCPN-source liveness
clock. `X` marks the OpenCPN source inactive immediately (don't wait for the
timeout). Keeping `w` as-is preserves the existing manual behavior.

### 1.3 APDAT gains a nav-source field (additive, but ALL parsers change together)

Append **one** field, `nav_source`, to the end of `~APDAT` (0=NONE, 1=GARMIN,
2=OPENCPN). Optionally a second field for standby status later; start with one.

> ⚠️ **Hard rule (from the autopilot skill):** APDAT is parsed *positionally* in
> three places. Adding a field means editing **all three in the same change**:
> - `Arduino/controller/publish.ino` (the `sprintf`)
> - `opencpn_plugin/.../AutoPilotLink.cpp` `ParsePacket()` + the `AutoPilotState` struct in `AutoPilotLink.h`
> - `Arduino/display/AutoPilot.cpp::parseAPDAT` (the physical TFT — do not forget it)
>
> Append at the end so existing field offsets don't shift. This is the one change
> that touches both machines *and* the display sketch; coordinate it as a single
> atomic protocol bump.

---

## 2. Controller firmware plan (Mac)

Files: `Arduino/controller/{garmin.ino, subscribe.ino, publish.ino, pid.ino,
controller.ino, telnet.ino, AutoPilot.h, AutoPilot.cpp}`.

### 2.1 Garmin UART: real line assembly + parse (`garmin.ino`)

- **Persistent line buffers.** `check_garmin()` currently keeps `line` local and
  has a 100 ms per-call budget, so a sentence split across two calls is lost.
  Add a `static` accumulation buffer per port; append chars until `\r`/`\n`, then
  dispatch a *complete* line. Keep the per-call time cap so tasks aren't starved.
- **Checksum + parse.** On a complete line: verify the `*XX` XOR checksum, then:
  - `RMB` → status A/V, dest lat/lon, arrival flag → feed the Garmin nav source (§2.4).
  - `XTE` → magnitude + steer L/R (kept for Option 3, §2.6).
  - `BOD` → leg course origin→dest (kept for Option 3).
  - `WPL`/`RTE`/`RMB`/`XTE`/`BOD` → also relay verbatim as `~APRX` (§2.3).
- Keep COM1 (`Serial1Port`, A0/A1) as the single live channel; leave
  `Serial2Port` reserved for Phase-2 host mode (research §"COM1/COM2").

### 2.2 Garmin write path (`garmin.ino`)

- Add `garmin_write_line(const char* nmea)` → writes `nmea` + `\r\n` to
  `Serial1Port`.
- **Pace bursts.** A whole route is many WPL lines; don't dump them in one call.
  Use a small FIFO of pending outbound lines, drained a few per `command_task`
  iteration so the FreeRTOS budget (research §3 firmware notes) is respected.

### 2.3 `~APTX` intake (`subscribe.ino`)

- In `dispatch_command()` add a branch: an `~APTX` frame → enqueue the inner
  NMEA line onto the §2.2 outbound FIFO. (Note: `~APTX` is *not* an `~APCMD`
  frame — handle it in `process_udp_command` alongside the `~APCMD,` prefix
  check, or give it its own small handler.)

#### Step 1a bench tests (covers §2.1–§2.3: relay + line buffer + FIFO + `~APTX`)

Run these after flashing the §2.1–§2.3 work, *before* moving on. The `~APRX`
relay itself (test C) needs a source on COM1 (real 276c, the emulator §4, or the
telnet `g` inject §2.7), so until 1b/§4 land, only **A** and **B** are exercisable
with zero extra hardware. Note: the `monitor/` tool the skill mentions is not
currently in the repo — use the inline UDP listener shown in test C.

**A. Regression — confirm the running pilot still works (highest priority).**
The only existing path touched is the `~APCMD` intake in `subscribe.ino`.
1. USB serial after boot shows the setup prints, including `Garmin A and B setup`.
2. SoberPilot AP comes up, display(s) connect, `~APDAT` still broadcasting.
3. From a display, change **mode / nav-enable / heading-adjust / set-WP** and
   confirm the controller reacts (motor + telemetry). In particular a normal
   `~APCMD,m2$` must still change mode — the new `~APTX` branch must not swallow
   non-`APTX` frames.

**B. `~APTX` write path — testable now without a Garmin.**
Join the Mac to SoberPilot, then send a framed NMEA line to the command port:
```bash
printf '~APTX,$GPWPL,3723.460,N,12158.940,W,WPT01*5E$' | nc -u -w1 10.20.1.1 8889
```
Watch the controller serial/telnet console for:
```
Garmin TX: $GPWPL,3723.460,N,12158.940,W,WPT01*5E
```
That proves the whole intake chain: `~APTX` matched *before* `~APCMD`, the outer
`$` stripped at the last byte (inner NMEA `$…*5E` preserved), enqueued to the
FIFO, drained to `Serial1Port`. The TX path does **not** checksum-verify (it
forwards verbatim), so the `*5E` value is irrelevant here — any string forwards.
With a scope or a second 3.3 V serial adapter on the A1 TX pin you'd see the
bytes + `\r\n` leave the UART. Negative check: a normal `~APCMD,m2$` still
changes mode (confirms B didn't break A).

**C. `~APRX` relay — deferred until a COM1 source exists (1b `g` inject / §4 / real 276c).**
With a source on COM1 (run the §2.7 `g` inject below, or the real Garmin on A0/A1
navigating a route), sniff UDP 8888 on the Mac:
```bash
python3 -c 'import socket; s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1); s.bind(("",8888))
while True:
    d,_=s.recvfrom(1024); m=d.decode(errors="replace")
    if m.startswith("~APRX"): print(m)'
```
Expect `~APRX` for `WPL/RTE/RMB/XTE/BOD` and **none** for `RMC/GGA/GLL/VTG` (the
relay filter), while the serial console shows `Garmin A:` for *every* line.

### 2.4 Nav-source arbitration (new module, the heart of ask #2)

Implement the §7.3 state machine in the controller. Suggested new unit
`Arduino/controller/navsource.ino` + state on the `AutoPilot` class.

- **Two sources, each with {active, last_update_ms}:**
  - GARMIN: active when RMB status=`A`; updated each RMB; dest = RMB lat/lon.
  - OPENCPN: active on `W` heartbeat; cleared on `X`; updated each `W`.
- **Liveness** = active AND fresh (≈6 s timeout each).
- **Selector** ∈ {NONE, GARMIN, OPENCPN}; **primary** is operator-selectable,
  **default GARMIN** (research §7.3 reasons). One live → follow it. Both live →
  follow primary, watch the other. Primary goes stale → **fail over** to the
  survivor. Primary returns → **hand back with hysteresis** (steady-live a few
  seconds) and **log every switch**.
- **Agreement check:** when both live, compare destinations within the §6
  epsilon; on mismatch follow primary but raise a flag in telemetry (don't blend).
- The selected source drives the existing `setWaypoint()` + `setMode(2)` machinery.
- **Arming guard** (research §5): a "Follow-Garmin armed" toggle. Armed → RMB `A`
  auto-engages nav; disarmed → only populates the waypoint, operator presses
  Enable. Preserves the project's "operator presses Enable" safety pattern.

### 2.5 State + telemetry (`AutoPilot.{h,cpp}`, `publish.ino`)

- Add `nav_source` (+ arming flag, + optional standby status) to the controller's
  `AutoPilot` class with locked getters/setters, following the existing pattern.
- Emit `nav_source` in APDAT (§1.3) — remember the three-parser rule.

### 2.6 Option 3 cross-track steering (later phase, `pid.ino` + `control_task`)

- In mode 2 today `setpoint = bearing_to_waypoint`. Change the desired-heading
  term to `leg_course ± clamp(Kxt × XTE, ±max)` using BOD (leg course) + XTE,
  refreshed on new leg (RMB dest ID change); blend back to plain
  bearing-to-mark near the waypoint. Tune `Kxt`/clamp via the `pid/` workflow.
- This is a **drop-in upgrade of the desired-heading term** — do it after Option
  1 (RMB-follow) is solid.

### 2.7 Tier-1 test hook (`telnet.ino`)

- Add a telnet command (e.g. `g<nmea-line>`) that injects a line into the exact
  path `check_garmin()` uses for a complete line. Lets us exercise relay +
  parse + arbitration with zero extra hardware (research §9 Tier 1). Cheap and
  high-value — add it early; it's the controller's own unit-test surface.

#### Step 1b bench test (covers §2.7: the telnet `g` inject hook)

This is the zero-hardware way to exercise the §2.1–§2.3 relay path: `g<nmea>`
runs the injected line through the exact complete-line dispatch (`check_garmin()`
→ checksum → relay filter → `~APRX`). Telnet to the controller
(`telnet 10.20.1.1 23`) and run the lines below while the test-C UDP sniffer (see
§1a tests) watches port 8888. The handler prints the outcome per line:

```
g$GPWPL,3723.460,N,12158.940,W,WPT01*0E                                  → ok - relayed as ~APRX
g$GPRMB,A,0.66,L,WPT01,WPT02,3729.000,N,12200.000,W,5.2,231.5,1.1,V*2B   → relayed
g$GPBOD,231.5,T,217.2,M,WPT02,WPT01*47                                   → relayed
g$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A    → filtered (status 2)
g$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47       → filtered (status 2)
g$GPWPL,3723.460,N,12158.940,W,WPT01*00                                  → dropped - bad checksum
```

The three "relayed" lines must appear as `~APRX,...$` in the 8888 sniffer; the
`RMC`/`GGA` and bad-checksum lines must **not** — proving the relay filter and
checksum gate end-to-end with no Garmin attached. (Checksums above are correct;
regenerate with an XOR of the chars between `$` and `*` if you edit a line.)

---

## 3. OpenCPN plugin plan (Navigator)

Files: `opencpn_plugin/autopilot_pi/{src/AutoPilotLink.cpp,
include/AutoPilotLink.h, src/AutoPilotPanel.cpp, src/autopilot_pi.cpp}`.

### 3.1 NMEA serialize / parse (in the plugin, where the route API + debugger live)

- **Serialize** an OpenCPN route → `WPL` (one per waypoint, position+name) +
  `RTE` (split ≤80 chars, names matching the WPL), correct XOR checksums, short
  deterministic IDs.
- **Parse** inbound `~APRX` lines: `WPL`/`RTE` (reassemble a route) and `RMB`
  (active dest → which route is active).

### 3.2 Send a route to the Garmin (ask #1, OpenCPN→Garmin)

- `AutoPilotLink::SendNmea()` wrapping each line as `~APTX`.
- `SendRoute(route)` → serialize → emit the WPL burst then the RTE.
- **Panel button "Send Route → Garmin"**, sibling to the existing "Set WP"
  button in `AutoPilotPanel` (the button plumbing pattern is already there —
  `ID_BTN_SEND_WP` / `OnSendWP`). Operator-triggered, **not** automatic (research §4).

### 3.3 Ingest + de-dup (ask #1, Garmin→OpenCPN, the round-trip problem)

Implement research §6 "RMB activates an existing route; WPL/RTE ingest only for
routes OpenCPN doesn't already have":

- On send, tag the RTE with a **short deterministic ID** (e.g. `OCPN01`) and keep
  a `shortID → route GUID` map; also record a **geometry fingerprint** (waypoint
  count + position sequence) + a send timestamp for the suppression window.
- On **RMB active**: try to match dest to an existing local route's waypoint
  (name-tag first, then geometry within epsilon — `ddmm.mmm` rounding means never
  match exactly). Match → **activate the existing route**, suppress creation.
- Only when **both keys miss** → genuinely Garmin-originated → build it from the
  buffered WPL/RTE via `AddPlugInRoute`.
- Reuse the project's optimistic-suppression idiom for the immediate echo
  (mirrors `m_suppress_until_ms` already in `AutoPilotLink`).
- **Open spike (resolve early on Navigator):** how does a *plugin* activate an
  existing route? `ocpn_plugin.h` exposes `GetActiveRouteGUID` and route
  create/update, but route *activation* appears only as `ActivateRoutePI` (a
  class method) — confirm the supported call (direct API vs. plugin-messaging
  `OCPN_RTE_ACTIVATE` vs. comm bridge) before building §3.3. This is the single
  biggest plugin unknown; spike it before committing to the dedup design.

### 3.4 OpenCPN as a steering source (ask #2)

- The plugin already learns the active leg in `SetActiveLegInfo`
  (`GetActiveWaypointGUID` + `GetSingleWaypoint`). Add a wxTimer that, while a leg
  is active, sends the `W` heartbeat (§1.2) every ~1–2 s, and sends `X` when the
  active leg clears. (Confirms the research §7.3 open question about cadence — the
  plugin controls its own timer, so ~1 s is achievable.)
- Also **push the route to the Garmin for display** when Following starts
  (research Scenario B), even though plain NMEA can't make the Garmin's own screen
  read "Navigating" (research §7.1 — accepted limitation, not a blocker).

### 3.5 Show who's steering (`AutoPilotLink` parse + `AutoPilotPanel`)

- Parse the new `nav_source` APDAT field into `AutoPilotState`.
- Surface "Following: GARMIN / OPENCPN (standby/offline)" on the panel so
  failover is visible (research §7.5).

---

## 4. Python Garmin emulator + test harness (Mac) — ask #3

New top-level dir, e.g. `emulator/` (sibling to `monitor/`, `pid/`). It stands in
for a **serial NMEA device**, not an IP endpoint (research §9): it talks to the
controller over the same 4800-8N1 UART the real 276c uses, via a 3.3 V FTDI/CP2102
tap on A0–A3 (common ground — **3.3 V logic, never 5 V**).

### 4.1 Modules

- `nmea.py` — sentence build + XOR checksum + parse for
  `RMC/GGA/WPL/RTE/RMB/XTE/BOD`. The authoritative §1 NMEA implementation on the
  Python side; unit-tested with `pytest`.
- `garmin_emulator.py` — the 276c state machine over `pyserial`:
  - streams idle `RMC/GGA`;
  - on command, emits `WPL/RTE` then cyclic `RMB`(+`XTE`/`BOD`), **advancing legs**
    with the arrival flag, ending with `RMB`→`V`;
  - **reads back** what the controller writes (the pushed route via `~APTX` → UART)
    so OpenCPN→Garmin push and the §6 de-dup round-trip are verified.
- `scenarios/` — the four canned scenarios from research §9:
  1. **Idle** — only RMC/GGA; assert relay filters them and AutoPilot does *not* engage.
  2. **3-waypoint route** — WPL/RTE burst, RMB advancing per leg + arrival, end →`V`;
     assert route-in, RMB-follow, leg auto-advance, disengage-at-end.
  3. **Accept-and-echo** — consume the controller's pushed route, replay it as if
     navigating; assert OpenCPN recognizes its own route (no duplicate).
  4. **Dual-source + failover** — drive emulated Garmin RMB **and** the OpenCPN
     source at once; assert single-source steering, no blend at leg-advance,
     mismatch flagged; kill the primary → assert failover within the timeout →
     hand-back with hysteresis. Run each source alone for single-device proof.
- `opencpn_stub.py` — a tiny UDP helper so scenario #4 doesn't need a real
  OpenCPN: sends `~APCMD,W…$` heartbeats / `X` to the controller and listens to
  `~APDAT` (reuse `monitor/monitorAutoPilot.py` decoding) to assert nav_source +
  failover. This is the Mac-side stand-in for "OpenCPN as a source."

### 4.2 How the emulator is used across tiers

- **Tier 1 (telnet inject, §2.7):** develop controller logic with no extra
  hardware — feed canned lines from `nmea.py` through the telnet `g` command.
- **Tier 2 (FTDI tap, primary):** real serial into the real controller; where most
  test effort goes. `opencpn_stub.py` provides the second source for arbitration.
- **Tier 3 (second ESP32):** out of scope for Python; build only if an untethered
  rig is wanted later.

---

## 5. APDAT protocol bump — coordinate as one atomic change

Because §1.3 touches **both machines and the display sketch**, treat it as a
mini-milestone with a fixed sequence so nothing parses a mismatched field count:

1. Agree the exact new field name/position/encoding here in §1.3.
2. Land controller `publish.ino` + display `parseAPDAT` together (Mac) —
   the display must not break when the controller adds the field.
3. Land plugin `ParsePacket`/`AutoPilotState` (Navigator).
4. Verify with `monitor/` that the new field appears and old consumers still
   decode.

Do this once, early (it's small), so later phases can rely on `nav_source` being
present everywhere.

---

## 6. Build order & cross-track dependencies

Following research §8 build order, annotated with **[Mac]** / **[Nav]** and
dependencies. Items at the same number can proceed in parallel.

| # | Work | Machine | Depends on | Testable by |
|---|---|---|---|---|
| 0 | Freeze §1 wire contract | both (agree) | — | — |
| 1a | `~APTX`/`~APRX` relay + line buffer + Garmin write FIFO | **Mac** | 0 | Tier-1 telnet inject; `monitor` sees `~APRX` |
| 1b | Tier-1 telnet `g` inject hook | **Mac** | 0 | telnet |
| 1c | Plugin NMEA serialize/parse + `~APTX`/`~APRX` plumbing | **Nav** | 0 | unit tests in plugin; raw-line round-trip |
| 2  | Python emulator core (`nmea.py`, idle + navigate, read-back) | **Mac** | 1a | FTDI tap → controller → `monitor` |
| 3  | OpenCPN→Garmin push: `SendRoute` + "Send Route" button | **Nav** | 1c | emulator read-back (scenario 3) once 2 lands |
| 4  | Follow-Garmin Option 1: RMB parse + armed engage | **Mac** | 1a | emulator scenario 2 |
| 5  | De-dup: RMB-activates-existing, ingest only unknown | **Nav** | 1c, 4, **§3.3 spike** | emulator scenario 3 |
| 6  | OpenCPN source `W`/`X` heartbeat + arbitration/failover state machine | both | 1c (plugin side), 4 (controller side) | emulator scenario 4 + `opencpn_stub.py` |
| 7  | APDAT `nav_source` bump + panel/TFT display | both | 6 | `monitor` + panel |
| 8  | Option 3 cross-track steering (XTE/BOD in `pid.ino`) | **Mac** | 4 | emulator scenario 2 with XTE; `pid/` tuning |
| 9  | (optional) APB; (Phase 2) GARMIN host mode / remote-activate | later | — | — |

**Critical-path note:** the emulator (2) unblocks most controller *and* plugin
testing, so it's worth front-loading on the Mac right after the relay (1a). The
plugin's de-dup (5) is gated by the route-activation spike (§3.3) — do that spike
during 1c so it doesn't block later.

---

## 7. Open questions to settle before/while building

1. **Plugin route activation API** (§3.3) — the one real unknown. Spike on the
   Navigator during step 1c.
2. **`nav_source` field shape** (§1.3) — one int now; add a standby-status field
   only if the panel needs it. Decide before step 7.
3. **Liveness timeouts & hysteresis constants** (§2.4) — start ~6 s liveness, a
   few seconds hand-back; tune on the bench with scenario 4.
4. **Short-ID scheme & truncation** (§3.1/§6) — confirm what the 276c keeps;
   until a real unit exists, the emulator enforces the same truncation so the
   round-trip is realistic.
5. **Garmin one-time setup** (research §2): COM1 = NMEA In/Out, 4800, GPWPL +
   GPRTE enabled — document in `navigator/README.md` when hardware arrives.

---

## 8. Where each piece of logic lives (quick map)

| Concern | Lives in | Machine |
|---|---|---|
| Garmin NMEA read/parse, `~APRX` relay, `~APTX` write | `controller/garmin.ino`, `subscribe.ino` | Mac |
| Nav-source arbitration / liveness / failover / agreement | `controller/navsource.ino` + `AutoPilot.{h,cpp}` | Mac |
| Cross-track steering (Option 3) | `controller/pid.ino`, `control_task` | Mac |
| Route serialize/parse, push button, ingest + de-dup | `autopilot_pi` (`AutoPilotLink`, `AutoPilotPanel`, `autopilot_pi.cpp`) | Nav |
| OpenCPN active-leg `W`/`X` heartbeat | `autopilot_pi` (`SetActiveLegInfo` + wxTimer) | Nav |
| `nav_source` telemetry | `publish.ino` + plugin `ParsePacket` + display `parseAPDAT` | both + display |
| Garmin emulator + test scenarios + OpenCPN source stub | `emulator/` | Mac |
