# Sea-trial plan — wind sensor and track (mode 2)

*Drafted 2026-09-07. Two independent trial series that can run on the same day.*

**W-series (wind)** commissions the masthead sensor: is the vane aligned, is the
speed calibrated, is the derived true wind trustworthy, does the head unit read well
underway.

**T-series (track)** is the sea-trial gate that
[`DeferredWork-PostSeaTrial.md`](DeferredWork-PostSeaTrial.md) puts in front of
Item A: *plain bearing-to-mark RMB-follow must be proven solid in real conditions
before cross-track steering is enabled*, because Item A is an upgrade of the
desired-heading term and you want a known-good baseline to compare against.

Both series also feed [`WindHoldModePlan.md`](WindHoldModePlan.md): W2/W4 decide
whether mode 3 is worth building at all, and W6 is the data run that sets its
tuning constants instead of guessing them.

---

## 0. Two corrections to carry, before anything else

1. **Telnet `n1` / `n0` no longer engage navigation.** The motor-enable switch on
   the controller board is the sole authority, and `check_motor_enable()` re-asserts
   it every 10 ms tick. `Phase-A-test-plan.md`'s "Telnet shortcuts: `n1` = Enable"
   is stale — telnet now only *answers* on `n`. Engage and disengage at the switch.
2. **`monitor/monitorAutoPilot.py` is gone** (removed in `7709125`). For raw
   telemetry use `nc -ul 8888` on the Navigator; it prints `~APDAT,...$` unparsed.

And the consequence worth briefing the crew on: **the boat engages at power-up if
the motor-enable switch is already closed.** That is the switch being authoritative,
working as specified. Start every power-up with the switch open.

---

## 1. Rig and observability

| Vantage | Where | Shows |
|---|---|---|
| **Telnet `p`** | Navigator: `telnet 10.20.1.1` → `p` | The one-stop status. Nav source · Motor enable (+ raw mV) · Navigation/Destination · Heading/Bearing/correction · Speed/Distance/Course/Location · **Rudder** · **Wind** (deg, kn, m/s, bft, **raw rev/s**, temp) · **True wind** (+ the SOG it used) |
| **Raw APDAT** | Navigator: `nc -ul 8888` | 1 Hz, all 39 fields, the practical logging tap |
| Controller serial | Mac, USB to the controller, 38400 | `[navsource]` promote/demote/switch lines — the only live *source* view. Needs a laptop tethered at the nav station |
| Head unit | cockpit | Mode / Target / Bearing / rudder box |
| Wind display | cockpit | the dial, and its three states |
| OpenCPN panel | Navigator | mode, source, waypoint, Send Rte / Follow |

Telnet `p` is the primary instrument for both series. Sample it on a fixed cadence
(every 10 s for track work — it matches the trim tick) rather than at random.

**Two readouts that do not exist and that you will want.** Neither is a blocker;
both are worth adding before the next trial day:

- `p` prints no `Kp`/`Ki`, so there is no way on the water to know which gains a
  past autotune left in flash.
- `p` prints no wind-board calibration (`voffset`, `calslope`, `caloffset`), so
  there is no way to confirm what a `d` or `k` actually left behind, or what
  baseline a fresh calibration is being applied on top of. The raw rev/s is on the
  line, but the constants it is corrected by are not.

---

## 2. Dockside pre-flight (every trial day)

- [ ] Motor-enable switch **open**. Power up. `p` → `Motor enable: off (≈0 mV)`,
      `Navigation: disabled`.
- [ ] All four boards up: display connected; `Rudder:` shows a number, not `no data`;
      `Wind:` shows a number, not `no data`; wind display shows the dial rather than
      NO LINK / NO WIND.
- [ ] GPS fix acquired — `p` shows `GPS` or `DGPS` and a satellite count.
- [ ] **Rudder centered and zeroed.** Rudder amidships (verify at the quadrant, not
      the wheel), then telnet `z`. `p` must then read `Rudder: 180.0`. There is no
      ack — that echoed 180.0 *is* the confirmation. Do this before any steering
      trial: the travel clamp and the virtual rudder position both hang off it.
- [ ] Record the day: date, crew, wind (forecast + observed), sea state, current
      state (tide stage), and which boards/firmware are flashed.

---

## 3. Safety and conduct

- **Hand on the motor-enable switch** for every engaged run. It is the kill switch
  and it is the fastest disengage available.
- Open water and sea room for everything. T6 in particular puts the boat into a
  deliberate circle near a mark — do it well clear of the mark, of traffic, of
  ground tackle and of shoaling.
- The autopilot is never unattended while engaged.
- **One variable at a time.** Especially in the W-series: a `d` nudge and a trim
  change in the same run tells you nothing.
- Abort criteria: any unexpected hard-over, any sustained oscillation that isn't
  settling, any confusion about what mode the boat is in. Disengage at the switch,
  note the state from `p`, then decide.

---

## 4. W-series — wind sensor

### W1 — Masthead link reliability *(runs continuously, all day)*

The base question everything else rests on: does the wind board stay associated
with SoberPilot at mast height, under way, heeled?

**Method.** No dedicated run — keep the wind display in view all day and note every
NO WIND, plus every `Wind: no data` seen on a `p` sample. Log time, duration, point
of sail, heel.

**Pass:** no dropouts, or dropouts rare enough and short enough to be invisible.

**Why it matters beyond the display:** mode 3's fallback trips on `isWindOk()`, which
is a 1 s receive timeout. A link that burps every few minutes gives a wind-hold mode
that keeps dropping to compass — the mode would be technically correct and
practically useless. If W1 is poor, fix the link before building mode 3.

### W2 — Vane alignment (the tack test) ★ headline

**`v` is not available to you underway** — it makes the vane's *current position*
zero and needs a hand on the vane, which is bench-only for a masthead unit. `d` is
the in-service form, and it corrects two errors `v` structurally cannot: mast
rotation relative to the hull, and aerodynamic bias from mast/mainsail upwash.
Neither is visible from the masthead.

**Method.** Steady breeze, flattest water available:

1. Settle close-hauled on **port** tack. Trim, then leave the trim alone.
2. Log AWA every 10 s for 2 minutes. Take the mean — call it `A_port`.
3. Tack. Settle on **starboard** with *identical* trim.
4. Log 2 minutes the same way — `A_stbd`.
5. The two angles off the bow should be equal. The offset is **half the difference**
   between them: `offset = (|A_port| − |A_stbd|) / 2`.
6. Send the **negation**: telnet `d<−offset>`. Bounds are ±180; the board
   range-checks before writing flash.
7. Confirm on the next `p` wind line. There is no ack — telnet's "ok" means *sent*,
   not applied. Watch the number change.
8. **Repeat steps 1–5.** The two tacks should now agree.

**Identical trim on both tacks is the whole experiment.** Sail it under the sail
plan you actually use. Expect to want two or three iterations; each `d` accumulates
(two nudges are two trims — they do not coalesce), so you can converge on it.

**Pass:** post-correction close-hauled AWA agrees within ~2–3° between tacks.

### W3 — Speed calibration data (`k`)

**Recommendation: do not fit the speed calibration on the water.** The documented
method — and the better one — is a car on a windless day with a GPS speed app. Wind
gradient, gusts, boat motion and current make a boat a poor calibration rig.

What the water *is* good for is a sanity check and a data record:

1. Log `raw rev/s` (from `p`) alongside indicated knots at several steady states.
2. If a handheld anemometer is aboard, log it at the cockpit for a rough
   cross-check — accepting that masthead and cockpit see different wind.
3. Note whether a calibration is already loaded (see the missing-readout gap in §1 —
   if in doubt, the honest answer is "unknown", and that itself argues for adding
   the readout before fitting).

The fit is `v = slope · n + offset` **in m/s**, bounds slope 0.1–10 and
|offset| ≤ 5. The offset is real physics, not a fudge: bearing friction and start-up
threshold mean the response is `a + b·n`, not a line through the origin.

**Why raw rev/s is on the wire at all:** it is upstream of the cup geometry and the
fitted slope/offset, so it is the only quantity you can fit *against*. Fitting
against already-corrected data would just re-fit your own correction.

### W4 — True wind sanity ★

True wind is derived on the controller from apparent + **SOG**. Confirm it behaves:

| Condition | Expect |
|---|---|
| Close-hauled | TWA **aft of** AWA, TWS **less than** AWS |
| Broad reach / run | TWA aft of AWA, TWS **greater than** AWS |
| Stopped, head to wind | true ≈ apparent |
| SOG < 0.8 kn (`GPS_SPEED_DEADBAND_KNOTS`) | true **reads as apparent** — correct, not a bug: `setSpeed()` zeroes the speed there rather than let GPS noise at anchor swing the angle |
| No GPS fix | `True wind: no GPS fix - no boat speed to subtract`, apparent still good |

**The one expected "wrong" reading:** on a day with real current, TWA/TWS carry the
current and the leeway, because the boat has no paddlewheel and SOG is the only
speed the system has. A consistent bias on a cross-current day is the design, not a
fault. It is right for a display and for steering a wind angle; it is wrong for
polars. Do not "correct" it.

**Pass:** all rows above behave; no discontinuities or angle spins as the boat
accelerates through the deadband.

### W5 — Wind display underway

1. **Readability.** Can you read the dial at a glance from the helm, in the light
   conditions you actually sail in?
2. **Damping.** 350 ms tau, 0.5° redraw deadband. In a seaway, does the needle read
   laggy, twitchy, or right? This is a subjective call and it is the only way to
   make it — log an opinion, not a number.
3. **The three states must stay distinguishable.** NO LINK (kill the controller or
   sail out of range) vs NO WIND (kill the masthead board) vs NO FIX in the
   true-wind fields alone. NO FIX is hard to force underway — check it dockside
   with the GPS antenna covered.
4. **Sector angles.** `RUN_HALF_ANGLE` is 60 (colour ends at AWA 120) and
   `NOGO_HALF_ANGLE` is 40 — both are properties of *this boat*, set from
   reasoning rather than measurement. Sail the boat and judge whether the no-go
   wedge matches where she actually stops going, and whether the asymmetric sector
   edge lands somewhere useful. Log the angles you'd prefer.

### W6 — Data run for mode 3 tuning

The point of this run is to replace the guessed constants in
[`WindHoldModePlan.md`](WindHoldModePlan.md) §4 with numbers from real gusts.

**Method.** `nc -ul 8888 > windlog-$(date +%H%M).txt` on the Navigator, then sail
three legs of ~10 minutes each, hand-steered as steadily as possible:

- close-hauled,
- beam reach,
- broad reach.

Note start/stop times per leg, and the sea state.

**What comes out of it:** the standard deviation and the timescale of AWA
fluctuation on each point of sail. Those set `WIND_HOLD_FILTER_TAU_MS` (must be
slower than the gust timescale), `WIND_HOLD_DEADBAND_DEG` (should sit around the
damped noise floor) and `WIND_HOLD_MAX_ERROR_DEG` (must be outside the normal
excursion or the fallback will nuisance-trip).

**Known limitation:** `~APDAT` is 1 Hz, so this logs the wind at 1 Hz even though the
masthead sends 5 Hz. `~APWND` is unicast to the controller, so the Navigator cannot
sniff the fast stream without a promiscuous capture. 1 Hz is adequate for gust
timescales of seconds — which is the timescale that sets these constants — but it
will not characterise mast whip.

---

## 5. T-series — track (mode 2)

### T1 — Compass-hold baseline (mode 1) ★ do this first

Mode 2's inner loop **is** mode 1 — the 100 Hz compass PID, with a setpoint that the
outer loop trims. So a mode-2 problem that is really a PID problem will be
indistinguishable unless you establish this baseline first.

**Method.** Engage in mode 1 on several points of sail (close-hauled, beam reach,
broad reach, run) for 5 minutes each. From `p` every 10 s log heading vs target.

**Record:** peak-to-peak heading excursion per point of sail; whether the boat hunts
(and at what period); how much standing helm is being carried.

**Pass:** settles without hunting on all points of sail. If it doesn't, stop the
T-series here — an autotune (`pat`, navigation disabled) or a gain revisit comes
first, and every later result would be contaminated.

### T2 — Mode 2 acquisition

Waypoint 0.5–1 nm off, from either source. Engage.

**Confirm:** `p` shows Mode 2 and the right `Nav source`; the wheel does **not**
lurch at engagement (`seedHeadingCommand()` starts `heading_command` at the current
heading corrected for known track error — a lurch means that seeding is wrong); the
boat settles onto a course within a minute or two.

### T3 — Track-trim convergence ★ headline

The outer loop ticks every **10 s**, gain **0.4**, clamp **10°/tick**.

**Method.** Long leg, steady conditions. Sample `p` every 10 s and log **Heading**,
**Bearing** (bearing-to-mark) and **Course** (COG) together.

**Confirm:**
- COG converges onto Bearing, and stays.
- The residual **Heading − COG** is the crab angle (leeway + current). It should be
  *stable*, not growing. That difference existing is correct.
- No oscillation. If it hunts, **record the period** — that is what sets a lower
  gain, and it is the single most useful number this trial can produce.

**Also confirm the low-speed gate.** COG is trusted with hysteresis (enter 1.0 kn,
exit 0.8 kn) and vector-damped at alpha 0.1. In light air, drop below 0.8 kn and
confirm trimming *stops* rather than chasing garbage COG — `cog_damped_valid` in the
APDAT stream, or the fact that Bearing stops moving.

### T4 — Cross-current leg ★

This is the design claim under test: because both sides of the trim comparison are
true-referenced and only the *error* crosses into the magnetic-frame
`heading_command`, declination, deviation, leeway and current all cancel out instead
of needing to be modeled.

**Method.** Pick a leg across a known current — a tide-table leg, or the channel on
a decent ebb. Sail it under mode 2.

**Pass:** the boat crabs (Heading offset from Bearing by a stable angle) while COG
stays on the mark. That is the loop working exactly as designed. If instead COG
sits persistently off the bearing, the trim gain is too low or the tick too slow for
the current strength — log both numbers.

### T5 — Leg advance, multi-leg route

3+ leg route from the Garmin or from OpenCPN (Send Rte, Follow on).

**Confirm at each waypoint change:** the destination updates on `p` and on the
panel; the turn onto the new leg is prompt, not a slow crawl over several trim
ticks (`setWaypoint()` refreshes the bearing and reseeds `heading_command` for
exactly this reason); no lurch.

### T6 — Near-mark behaviour ⚠ sea room

The documented end-of-route policy is deliberate: at end-of-route the controller
**keeps steering the last commanded waypoint**, so the boat circles the last mark
rather than sailing off on a held heading. The known rough edge, explicitly not
fixed, is that bearing-to-mark goes noisy as range → 0 — small position jitter
swings the bearing wildly.

`DeferredWork-PostSeaTrial.md` Item B says to revisit "if sea trials show the
near-mark steering is too aggressive". **This is that trial.**

**Method.** Open water, no mark, no traffic, nothing to hit. Set a waypoint on open
water and sail to it under mode 2. Let it arrive and keep going.

**Record:** the range at which steering starts getting erratic; how hard the helm
works; the radius and character of the circle; whether it is something you'd be
comfortable with if it happened unnoticed.

**Outcome decides** whether Item B needs a fix (min-range floor, or Item A's XTE
steering), or whether the current behaviour is acceptable as-is.

### T7 — GPS fix-loss fallback

Hard to force underway. If a natural dropout occurs, confirm: mode 2 → 1 holding
the current heading, **navigation stays engaged**, and mode 2 resumes when the fix
returns (unless you touched something in between, which cancels the auto-resume by
design). Otherwise verify dockside with the antenna covered.

### T8 — Source hand-off underway

Phase A was fully bench-verified; this just confirms it at sea. With both sources
live, Garmin wins. Stop the Garmin → ~6 s → failover to OpenCPN, waypoint flips,
mode and nav-enable unchanged. Watch the Mac serial `[navsource]` lines, or
`Nav source:` on `p`.

---

## 6. Decision gates — what each result unblocks

| Gate | Needs | Unblocks |
|---|---|---|
| Wind data is trustworthy | W2 + W4 pass | Mode 3 worth building at all |
| Mode 3 constants | W6 log | `WindHoldModePlan.md` §4 set from data, not guessed |
| Mode 3 fallback is viable | W1 clean | Otherwise fix the masthead link first |
| **Sea-trial gate satisfied** | **T1–T5 pass** | **Item A: flip `XTE_STEERING_ENABLED` to 1, rebuild, re-trial against this baseline** |
| Near-mark policy | T6 | Whether Item B (arrival handling) needs work |

Note the ordering that falls out of this: **the T-series gates Item A, and the
W-series gates mode 3, and the two do not gate each other.** They can run on the
same day, and mode 3 can be built while cross-track is still waiting on track
results.

---

## 7. Log sheet

Copy per run.

```
Date ______  Crew ______________  Firmware: ctrl ____ disp ____ wind ____ rud ____
Wind: forecast ______  observed ______   Sea state ______   Current/tide ______

Run ID ____  Trial ____  Start ____  End ____
Point of sail ____________  Sail plan ____________

  time   heading  target/bearing  COG   AWA   AWS   TWA   TWS   SOG   rudder
  ____   _______  ______________  ____  ____  ____  ____  ____  ____  ______
  ____   _______  ______________  ____  ____  ____  ____  ____  ____  ______

Calibration sent this run:  d____  k____,____   confirmed on p? Y/N
Dropouts (time / duration / what):

Observations:

Result: pass / fail / inconclusive        Follow-up:
```

---

## 8. Open questions to settle on the water

1. **T3:** does gain 0.4 / 10 s hunt on this boat? If so, at what period?
2. **T4:** how much current can the trim loop hold out against before COG sits
   persistently off the bearing?
3. **T6:** is the circle acceptable, or does Item B need pulling forward?
4. **W2:** how many `d` iterations to converge, and does the offset hold across
   different wind strengths (upwash varies with trim and heel — if it doesn't hold,
   a single scalar offset is the wrong model and that is worth knowing early).
5. **W5:** are `NOGO_HALF_ANGLE` 40 and `RUN_HALF_ANGLE` 60 right for this boat?
6. **W6:** what is the real gust timescale — i.e. is a 5 s filter tau right, or
   should mode 3 be slower still?
