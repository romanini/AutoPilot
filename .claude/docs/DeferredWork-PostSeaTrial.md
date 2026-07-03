# Deferred work — implement AFTER on-water testing

Two upgrades intentionally held back until the current system (Phase A engagement
selector + Phase B `nav_source` telemetry + Phase C route ingest) has been
**tested on the water**. Neither is a bug fix; both change steering/behavior, so
they should land against a baseline we already trust from real sailing.

> **Gate:** do not start either item until the operator has sea-trialled the
> existing behavior and is happy with it. Order between the two is open — item B
> (arrival) is a safety/UX gap; item A (cross-track) is a steering-quality upgrade.

Companion docs: [`RouteImplementationPlan.md`](RouteImplementationPlan.md) §2.6,
[`NavigationEngagePlan.md`](NavigationEngagePlan.md) §3 TODO,
[`RouteCommunicationResearch.MD`](RouteCommunicationResearch.MD).

---

## Item A — Option 3 cross-track (XTE) steering

**Machine:** Mac (controller firmware). **Files:** `Arduino/controller/pid.ino`,
`controller.ino` (`control_task`), possibly `navsource.ino` /
`AutoPilot.{h,cpp}` to carry leg-course + XTE. Tuning via `pid/`.

**Why deferred:** it's a drop-in upgrade of the desired-heading term and should
only be attempted once plain RMB-follow (bearing-to-mark) is proven solid in real
conditions — you want a known-good baseline to compare against.

**What it is.** Today in mode 2 the PID setpoint is `bearing_to_waypoint` (aim
straight at the mark; current drifts push you off the rhumb line). Option 3 steers
the **leg line** instead:

```
desired_heading = leg_course ± clamp(Kxt × XTE, ±max)
```

- `leg_course` = the BOD sentence (bearing origin→destination).
- `XTE` = cross-track error (sign = which side of the line), from the XTE sentence.
- Refresh `leg_course` on each **new leg** (detected by RMB dest-ID change).
- **Blend back** to plain bearing-to-mark as range → 0, so you don't chase a
  line through the waypoint.
- Tune `Kxt` and the clamp with the offline `pid/` workflow, then on the water.

**Inputs already available.** The controller already relays/parses RMB; XTE and
BOD arrive on the same Garmin channel (garmin.ino filters relay
WPL/RTE/RMB/XTE/BOD). The emulator emits XTE+BOD too, so this is benchable before
sailing.

**Dependencies / risks.**
- Only affects mode 2 (waypoint). Compass-hold (mode 1) is untouched.
- Must degrade gracefully when XTE/BOD are missing/stale → fall back to
  bearing-to-mark (don't steer on stale cross-track).
- Interacts with the GPS-loss compass-hold fallback in `setFix()` — keep that
  path intact.

**Test.** Bench with the emulator: inject an offset start so XTE ≠ 0, confirm the
boat converges to the leg line, not just points at the mark; confirm leg-advance
re-bases `leg_course`; confirm near-mark blend; confirm fallback when XTE/BOD drop.

---

## Item B — Arrival / end-of-route behavior

**Machine:** Both. **Controller (Mac):** make arrival an explicit event +
telemetry flag (`navsource.ino`, `AutoPilot.{h,cpp}`, `publish.ino`).
**Navigator (plugin) / Mac (display):** show the arrival alert; handle the
OpenCPN keep-active case.

**Why deferred:** the *implicit* current behavior is safe enough to sail with —
this hardens it. Decisions were sketched but not chosen on 2026-06-29.

**Current implicit behavior (the baseline this replaces).**
- **Garmin:** final-WP arrival → RMB status `V` → GARMIN source clears → selector
  NONE → `setMode(1)` holds current heading (compass-hold), **nav stays enabled** →
  boat sails straight on present heading indefinitely.
- **OpenCPN:** depends on OpenCPN's arrival setting. Route auto-deactivates →
  heartbeat stops → ~6 s timeout → same compass-hold. **But** if the route stays
  active on the final WP → heartbeat continues → controller keeps Mode 2 on a
  waypoint now *behind* the boat → it **circles/oscillates** the mark. (Bad;
  driven by an OpenCPN setting, not our code.)

**Gaps to close.**
1. **Explicit arrival event** rather than a side effect of the selector going
   NONE — so we can act on "we arrived" distinctly from "source lost / signal
   dropped."
2. **Kill the OpenCPN circle case** — detect arrival (range < radius, or
   dest-behind-beam) and stop steering to a mark we've passed even if the
   heartbeat keeps coming.
3. **Arrival action** — likely revert to **heading-hold on current heading + raise
   an arrival alert** (marine convention: never silently circle, never drop
   steering entirely). Nav stays enabled; operator decides next.
4. **Signalling** — an arrival flag in telemetry → panel/TFT indicator, optional
   buzzer. If telemetry: append as one atomic APDAT bump across all three
   positional parsers (same discipline as the `nav_source` Phase B field —
   controller `publish.ino`, `display/AutoPilot.cpp`, plugin `AutoPilotLink`).

**Dependencies / risks.**
- Touches the selector's NONE branch — keep the "never auto-enable / never
  disable nav" invariant. Arrival changes *mode/heading + alert*, not nav-enable.
- The circle case needs geometry (range + bearing-past-mark); reuse the
  controller's existing distance/bearing helpers.
- If it adds a telemetry field, it's a protocol bump → coordinate Mac + Navigator
  cutover exactly like Phase B (tolerant parse of the trailing field).

**Test.** Emulator route-end (`--speed` fast-forward) → confirm explicit arrival +
alert + heading-hold, nav still enabled. Separately force the OpenCPN
keep-route-active case → confirm no circling. Verify the alert reaches panel/TFT
(and buzzer if added).
