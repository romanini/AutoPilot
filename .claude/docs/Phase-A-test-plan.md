# Phase A test plan — navigation-engagement selector

Bench/integration checklist for the two-source selector in
`Arduino/controller/navsource.ino` (see [`NavigationEngagePlan.md`](NavigationEngagePlan.md) §2–3).
All Group A tests were run from OpenCPN alone; B/C/D use the Garmin emulator.

> **The one rule under test:** navigation is *never* auto-enabled. In every case,
> before you press Enable, controller state must read `Navigation: disabled` even
> when Mode has gone to 2. Mode-2-with-nav-off is the intended "armed and waiting"
> state.

## Observability — where to look

`nav_source` is **not** in APDAT yet (that's Phase B), so the OpenCPN panel can't
show "who's steering." Read the source from telnet `p` or the Mac serial log.

| Vantage | Where / how | Shows |
|---|---|---|
| Serial log | **Mac**, `/dev/cu.usbmodem3485187A7A942` @ 38400 (IDE Serial Monitor or `screen … 38400`) | live `[navsource]` promote/demote/switch lines — the only live *source* view |
| Telnet `p` | **Navigator**, `telnet 10.20.1.1` → `p` | authoritative state: `Nav source:`, Navigation on/off, Destination/mode, waypoint |
| APDAT monitor | **Navigator**, `monitor/monitorAutoPilot.py` | mode, waypoint_set, wp_lat/lon, nav_enabled (not source) |

Telnet shortcuts: `n1` = Enable, `n0` = Disable, `p` = status.

Ports (this bench): controller CDC `/dev/cu.usbmodem3485187A7A942` (38400) · FTDI
Garmin tap `/dev/cu.usbserial-AB7DHVRF` (4800). Emulator base command (run in
`emulator/`, needs the `.venv`):

```bash
.venv/bin/python garmin_emulator.py --port /dev/cu.usbserial-AB7DHVRF --navigate-after 2
```

Idles (RMC/GGA, dropped by the relay → source stays NONE) for `--navigate-after`
seconds, then streams RMB `'A'` every second. At default speed it never reaches
the waypoint → a steady GARMIN-live stream until Ctrl-C. Crank `--speed` only
where noted below.

**Reset between tests:** Ctrl-C the emulator, uncheck Follow in OpenCPN, telnet
`n0` then `m1`. Baseline = nav disabled, Mode 1, source NONE.

---

## Group A — OpenCPN only, no Garmin

- [x] **A1 — Lone Set WP, Follow off.** One Set WP → waypoint visible, **Mode not 2**, `Nav source: NONE`, nav disabled. A 2nd lone click >3 s later still does not promote. (`show_pending` is silent on serial — verify via `p`/monitor.)
- [x] **A2 — Follow on (sustained `w`).** Serial `OPENCPN live` → `selected -> OPENCPN`; `p` = Mode 2 / OPENCPN / **nav disabled**. Press Enable → nav enabled, steering starts.
- [x] **A3a — Stop Follow via `X`.** Uncheck Follow → serial `OPENCPN cleared ('X')` → `selected -> NONE (compass-hold fallback)`; Mode 2→1, nav stays enabled. A lone Set WP within 3 s must **not** re-promote.
- [x] **A3b — Heartbeat death (no `X`).** Kill heartbeat abruptly → ~6 s → serial `OPENCPN stale` → NONE → Mode 1.
- [x] **A4 — Send Rte.** Auto-checks Follow → streams `w` → Mode 2 / OPENCPN / nav disabled → press Enable.

## Group B — Garmin only (emulator)

- [x] **B1 — RMB 'A' → GARMIN live.** Run EMU → serial `GARMIN live (RMB 'A')` → `selected -> GARMIN`; `p` = source GARMIN, waypoint ≈ 37.700,-122.520, **nav disabled**. `n1` = Enable.
- [x] **B2 — RMB 'V' (route end).** `EMU --speed 5000` fast-forwards the route; at the last wpt serial `GARMIN cleared (RMB 'V')` → `selected -> NONE (compass-hold fallback)`; Mode 1, nav stays enabled if you enabled it.
- [x] **B3 — RMB stops abruptly.** EMU steady → confirm live → Ctrl-C → ~6 s → serial `GARMIN stale (no RMB), demoted` → NONE → Mode 1.
- [x] **B4 — Leg advance.** `EMU --speed 1200` → waypoint steps WPT02 (37.700,-122.520) → WPT03 (37.600,-122.600), staying GARMIN / Mode 2. (Dest-only change is silent on serial — watch `p`/monitor.)

## Group C — Dual-source arbitration (emulator + OpenCPN)

> Use an OpenCPN waypoint whose lat/lon is clearly different from the SF demo
> route, so `p` coords reveal which source owns the waypoint.

- [x] **C1 — Garmin wins.** OpenCPN Follow first (source OPENCPN) → start EMU → serial `GARMIN live` → `selected -> GARMIN`; waypoint flips to Garmin coords and the still-streaming OpenCPN `w` does not move it back.
- [x] **C2 — Failover Garmin→OpenCPN.** From C1, Ctrl-C the emulator (Follow still streaming) → ~6 s → `GARMIN stale` → `selected -> OPENCPN`; waypoint back to OpenCPN coords, Mode 2, nav unchanged.
- [x] **C3 — Failover OpenCPN→Garmin.** OpenCPN Follow only → start EMU → immediate `GARMIN live` → `selected -> GARMIN`; waypoint → Garmin coords.
- [x] **C4 — Both drop.** Ctrl-C emulator + uncheck Follow → `selected -> NONE (compass-hold fallback)`; source NONE, Mode 1.

## Group D — Safety invariants

- [x] **D1 — Never auto-enable.** EMU → GARMIN live / Mode 2 but **do not** Enable. `p` = `Navigation: disabled`, motor idle (control_task steers only when nav enabled — `controller.ino:97`). Then `n1` engages.
- [x] **D2 — Manual nav not hijacked.** Telnet `w37.5,-122.4` + `m2` + `n1` (source NONE, Mode 2, nav on). Wait >6 s → still Mode 2/enabled (selector's NONE fallback is gated on `applied_source`, so it leaves operator Mode 2 alone). A lone OpenCPN Set WP updates the shown waypoint but does not disengage.
- [x] **D3 — Post-clear no auto-promote.** Follow on → uncheck (`X`) → single Set WP within 3 s → no `OPENCPN live`; `p` stays source NONE / not Mode 2. (= A3a.)

---

_All groups passed on the bench. Selector logic in `navsource.ino` verified end
to end; Phase B adds the `nav_source` field to APDAT so the panels can display
the active source._
