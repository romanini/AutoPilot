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

**STATUS: IMPLEMENTED BUT DISABLED-BY-DEFAULT, 2026-07-04.** Landed early
(ahead of the sea-trial gate) as a compile-time-only feature: `#define
XTE_STEERING_ENABLED 0` in `Arduino/controller/controller.ino` gates the whole
thing out (new file `crosstrack.ino` compiles to nothing, the XTE/BOD parsing
in `garmin.ino` compiles to nothing, the blend hook in `control_task` compiles
to nothing) — a disabled build is byte-for-byte the same size as before this
change. No runtime toggle; flip the `#define` to `1`, rebuild, reflash to turn
it on. **Do not flip it on before the sea-trial gate below is satisfied** —
landing the code early doesn't change the reasoning for holding off on
enabling it.

**Machine:** Mac (controller firmware). **Files:** `Arduino/controller/crosstrack.ino`
(new — state + blend math), `garmin.ino` (XTE/BOD parsing, gated), `controller.ino`
(the feature flag + `control_task` hook, gated). Tuning constants
(`XTE_KXT_DEG_PER_NM`, `XTE_MAX_CORRECTION_DEG`, `XTE_BLEND_RADIUS_NM`) live at
the top of `crosstrack.ino` as placeholders — tune via the `pid/` workflow
before ever enabling on the water.

**Why still deferred (enabling, not writing):** it's a drop-in upgrade of the
desired-heading term and should only be *turned on* once plain RMB-follow
(bearing-to-mark) is proven solid in real conditions — you want a known-good
baseline to compare against.

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

**Test.** Bench with the emulator or telnet `g` inject (both route through the
same `garmin_dispatch_line` the parsing hooks into, with the flag flipped on
for the test build): inject an offset start so XTE ≠ 0, confirm the boat
converges to the leg line, not just points at the mark; confirm near-mark
blend; confirm fallback when XTE/BOD drop or go stale (`XTE_STALE_MS`, matches
`GARMIN_NAV_TIMEOUT_MS`); confirm OpenCPN-sourced nav (`nav_source == 2`) is
never affected, only live GARMIN mode-2 nav is. Note: `leg_course` is taken
directly from the latest BOD each cycle rather than explicitly re-based on an
RMB dest-ID change — BOD is constant for a given leg, so this is equivalent in
practice, but worth confirming on the bench with a real multi-leg route.

---

## Item B — Arrival / end-of-route behavior

**STATUS: DECIDED AND IMPLEMENTED 2026-07-03** (ahead of the sea-trial gate
below — the operator chose to pull this one forward). See
`Arduino/controller/navsource.ino`. Kept here for history; the "never circle"
reasoning below was the original analysis and was explicitly overridden.

**Decision:** at end-of-route (any source going non-live, whether from genuine
final-waypoint arrival or the source just going quiet), the controller now
**keeps steering at the last commanded waypoint** — mode and waypoint are left
untouched, only the announced `nav_source` telemetry drops to `NONE`. In
practice this means the boat circles the last mark rather than holding a
heading. This applies uniformly to **both** Garmin and OpenCPN sources — no
attempt is made to distinguish "arrived at the final leg" from "source went
silent for another reason" (e.g. operator-cancelled navigation on the Garmin
also results in circling, not compass-hold). No new telemetry/alert flag was
added — the circling itself is the operator-visible signal.

**Why this reverses the original plan below:** the original concern (orbiting a
fixed point is a hazard — mark, mooring, ground tackle, traffic, shoaling) was
judged not to outweigh the benefit of the boat waiting at the last waypoint
instead of sailing off indefinitely on whatever heading it happened to hold at
arrival. Revisit if sea trials show the near-mark steering is too aggressive
(see rough edge below).

**Known rough edge, not fixed:** bearing-to-mark steering gets noisy as range →
0 (small position jitter swings bearing wildly) — the same oscillation this doc
originally warned about for the OpenCPN keep-active case. A fix (min-range
floor, or Item A's cross-track/XTE steering) is a separate follow-on.

**Original analysis (superseded, kept for context):**

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
