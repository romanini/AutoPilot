# Follow-up note (Navigator) — add a name-tag (Key1) de-dup test for Phase C

**Context.** The Phase C de-dup in `AutoPilotLink::FlushInboundRoute()` has two
match keys (RouteImplementationPlan §3.3):

- **Key1 — name-tag:** inbound RTE `route_id` (`m_rte_id`) is found in
  `m_sent_routes` → we pushed this exact route → activate the recorded GUID.
  (Plugin log: `AutoPilot: de-dup Key1 match id='…' → guid …`.)
- **Key2 — geometry:** inbound waypoint positions match a local route within
  `ROUTE_MATCH_EPSILON` → activate that GUID.
  (Plugin log: `AutoPilot: de-dup Key2 geometry match → guid …`.)

**The gap.** The C-1 test in `Phase-A-test-plan.md`/Phase-C uses the plain CLI
emulator (`garmin_emulator.py`), which always navigates its built-in `DEMO_ROUTE`
under `route_id = "GARMN1"` with names `WPT01…`. That id is **not** what the
plugin stamped on the route it sent (its own short id, e.g. `OCPN01`), so C-1 can
only ever exercise **Key2 (geometry)**. Key1 is never hit — so a bug in the
name-tag path would pass C-1 unnoticed.

**Prerequisite.** Land the `FlushInboundRoute()` use-after-free fix first (hold the
`GetHostApi()` `unique_ptr` in a named local) — Key1 lands in the same
activate-existing branch that crashed C-1.

---

## The test — reuse `scenario3_accept_echo.py` (no new code needed)

That scenario already does the right thing: it **collects** the plugin-pushed
`WPL`+`RTE` from the controller (capturing the plugin's actual `route_id`), then
**replays** it with `--replay` (default on) — re-emitting `RMB`/`XTE`/`BOD` for
*that* route, preserving its `route_id`. Because the replayed RTE carries the same
short id the plugin recorded in `m_sent_routes`, the inbound `m_rte_id` matches →
**Key1 fires** (no reliance on geometry).

### Steps (Navigator + Mac, FTDI tap wired)
1. Apply the crash fix, rebuild the flatpak, clear the load stamp
   (`rm ~/.var/app/org.opencpn.OpenCPN/config/opencpn/load_stamps/libautopilot_pi`),
   relaunch OpenCPN **from a terminal** so plugin `wxLogMessage` lines are visible.
2. OpenCPN: load **and activate** a route, open the AutoPilot panel, note the
   route count in Route Manager.
3. Mac: `cd emulator && .venv/bin/python scenarios/scenario3_accept_echo.py --port /dev/cu.usbserial-AB7DHVRF`
4. OpenCPN: press **Send Rte**. The script logs the received `WPL`/`RTE` (incl.
   the `id=…`), validates it, then begins the replay (`RMB` per leg).
5. The replay's `RMB` returns to the plugin as `~APRX` → `FlushInboundRoute()`.

### Expected
- Plugin log shows **`de-dup Key1 match id='…'`** (not the Key2 line), then
  `activated existing route …` / `already active, no-op`.
- Route Manager count is **unchanged** — no duplicate created.
- No crash.

### Distinguish it from C-1
- **C-1 (CLI emulator, `GARMN1`)** → expect the **Key2 geometry** log line.
- **This test (scenario3 replay, plugin's own id)** → expect the **Key1** log line.

Running both proves each match key independently: if Key1 fails here but C-1
passes, the bug is in the `m_sent_routes` bookkeeping (does `SendRoute()` key the
map by the *same* short id it stamps into the RTE, and does that id survive the
round-trip?). If Key2 fails in C-1 but Key1 passes here, the bug is the geometry
epsilon.

### Optional hardening
- Consider a one-line assertion/log tag in each branch that names the key, so CI
  or a scripted run can grep the outcome deterministically.
- Real-Garmin caveat: a physical 276c may truncate/alter the short id on the
  round-trip, dropping Key1 → Key2 is the safety net. Worth a comment in
  `FlushInboundRoute()` noting Key2 must remain robust for that reason.
