# Road test — track feature + wind calibration (van, no boat)

*Drafted 2026-09-08. Supersedes the W2/W3/W6 and T2/T3/T7 sections of
[`SeaTrial-WindAndTrack-Plan.md`](SeaTrial-WindAndTrack-Plan.md) for anything
doable on land — that document remains the plan for the boat.*

Rig: **controller + display unit + wind head** in the van, vane on a pole outside.
No rudder board, no motor, no Navigator. Laptop joined to SoberPilot.

**Bring the laptop.** It is not optional: the displays send only `a`/`m`/`t`
(buttons, not a keyboard), so there is no other way to set a waypoint or send a
wind calibration. Everything below assumes it.

---

## 0. What a van can and cannot prove

**The van is a better calibration rig than the boat**, for both wind numbers. On a
straight run in still air the apparent wind is *exactly* on the bow at *exactly* the
GPS speed — two known references you can never get afloat. The boat's tack test only
ever yields a relative correction; this yields absolute answers.

**The track loop, though, runs open-loop in a van.** On the boat, `heading_command`
drives the rudder, the boat turns, COG changes, and the error closes. In the van
nothing steers, so the loop never closes and `heading_command` will simply keep
trimming. That is expected, and it is not a failure.

So the track test here is a **sign, magnitude and plumbing test** of the outer loop —
which is precisely what a bench cannot check and what is most likely to be wrong.
A sign error in the trim is invisible until the day it steers a boat the wrong way.

| Testable in the van | Must wait for the boat |
|---|---|
| Bearing/distance maths, waypoint updates | Closed-loop convergence, settling, hunting |
| Trim **direction** and **rate** (0.4 × error, ≤10°/10 s) | PID tuning, rudder, motor |
| COG trust gate (1.0 kn in / 0.8 kn out) | Leeway and current behaviour |
| GPS fix-loss fallback and resume | Near-mark circling under sail |
| Mode transitions, arrival/circle policy | Rig-specific vane bias (upwash, mast rotation) |
| **Vane alignment, absolute** | Final speed trim vs the handheld meter |
| **Speed calibration, absolute** | |
| True-wind derivation (see W5) | |

---

## 1. Mounting the vane ⚠ read before drilling anything

- **Height beats position.** A van roof carries a big separation bubble; the front
  edge is the worst of it. Get the vane **at least 1 m above the roof**, 1.5 m if the
  pole will take it. Clean air matters more than anything else in this document —
  every number below inherits the mounting error.
- **Rigid.** Any pole whip or vibration corrupts both the vane and the cups. Brace
  or guy it. Re-check the clamps at *every* stop.
- **Align the vane's bow mark to the vehicle centreline** by sighting down the van.
  Get it as close as you can by eye; step W3 measures whatever is left.
- **Measure the total height and write it on a sticky note on the dash.** You are
  about to drive around with a 1.5 m mast. No garages, no drive-thrus, no low
  branches. This kills the "test GPS loss in a parking garage" idea — see W6/T6 for
  the alternative.
- **Speed ceiling 45 mph.** That is ~39 kn of apparent wind through 3D-printed cups.
  It is inside the electronics' range (the firmware's sanity limit is 100 rev/s ≈
  175 kn) but not obviously inside the *plastic's*. Inspect the cups for cracks at
  each stop.

**Motor:** confirm the steering motor is **disconnected**. Navigation will be enabled
during the track test and `motor_control_loop()` drives the H-bridge pins regardless
of whether anything is attached.

---

## 2. The one prerequisite that will stop you dead

**The motor-enable switch must be wired and closable in the van.**

`check_motor_enable()` reads A6 every 10 ms and enforces `navigation_enabled ==` the
switch. With no switch, A6 floats at 0 → navigation off, permanently. And with
navigation off:

- `gps_track_trim()` returns immediately — **the entire track test does nothing**, and
- the display's MODE button won't change mode (it only arms auto-tune when disabled).

There is no software override. `n1` on telnet is retired and answers with a message
telling you to use the switch; there is no `n` case on the UDP side at all.

So: bring the controller board with U7 wired, or wire a temporary switch (or a jumper)
across it. Verify before leaving — `p` must show `Motor enable: on (≈2500 mV)`.

**And note it engages at power-up if the switch is already closed.** Start with it open.

---

## 3. Laptop setup (do this at home, once)

Join SoberPilot. Then:

```bash
cd /Users/romanini/dev/AutoPilot/firmware/experiments/roadtest
python3 roadtest.py log --host 10.20.1.1 --out ~/Desktop/drive-$(date +%m%d-%H%M).csv
```

That is the whole data-collection story — **one command, then drive.** It logs at
1 Hz from two sources at once:

- **UDP 8888** — the broadcast `~APDAT`, all 39 fields. This carries field 14
  (`target`, the live steering setpoint) which the track test needs and which telnet
  never prints.
- **telnet 23** — polls `p` once a second purely to scrape the anemometer's **raw
  rev/s**. That number is deliberately *not* on `~APDAT` (it is calibration data, so
  it stays controller-side), and it is the only quantity a speed calibration can
  honestly be fitted against, because it sits upstream of the slope and offset you
  are trying to measure.

**The controller's telnet server takes one client.** While the logger runs you cannot
open an interactive telnet session. Send commands over UDP instead:

```bash
echo -n '~APCMD,d-6.2$' | nc -u -w1 10.20.1.1 8889
```

Set up these shell aliases before you go, so nothing needs typing at the roadside:

```bash
apcmd() { echo -n "~APCMD,$1\$" | nc -u -w1 10.20.1.1 8889; }
# apcmd 'd-6.2'        vane trim, degrees
# apcmd 'k1.05,-0.12'  speed calibration, slope,offset (m/s)
# apcmd 'm2'           mode 2
# apcmd 'm1'           mode 1
```

---

## 4. Pre-drive checklist

- [ ] Motor-enable switch **open**. Power up. Motor **disconnected**.
- [ ] Laptop on SoberPilot; `ping 10.20.1.1` answers.
- [ ] `telnet 10.20.1.1` → `p`:
  - [ ] GPS fix (`GPS` or `DGPS` + satellite count). Wait for it — a cold fix can
        take minutes and nothing below works without one.
  - [ ] `Wind:` shows an angle and a speed, **and a `(raw N.NNN rev/s)`**. If rev/s
        is missing the speed calibration cannot be fitted.
  - [ ] `Rudder: no data` — **expected**, there is no rudder board. The display's
        rudder box will read the same.
- [ ] Spin the cups by hand → speed rises. Turn the vane by hand → angle follows.
- [ ] Note the current calibration state. There is **no readout** for `voffset`,
      `calslope` or `caloffset` (a known gap — see
      [`SeaTrial-WindAndTrack-Plan.md`](SeaTrial-WindAndTrack-Plan.md) §1). If this
      head has never been calibrated they are 1.0 / 0.0 / 0. **If you are not
      certain, that uncertainty is itself a reason to fit from rev/s** — which is what
      the tool does, and it is immune to whatever is already loaded.
- [ ] Pole clamps torqued; height noted on the dash.

---

## 5. Part 1 — Track test

### T1 · Set the destination (stationary, engine off)

Pick a waypoint **5–15 km down a long straight road you will actually drive**. Google
Maps → right-click → click the coordinates to copy.

```
telnet 10.20.1.1
w37.412345,-122.098765      → ok
m2                          → ok, then the status echo
p
```

Confirm on `p`: `Destination: waypoint 37.412345,-122.098765`, and a sane `Distance`.

> Telnet `w` calls `setWaypoint()` directly, so one command is enough. The UDP `w`
> is different — it routes through the nav-source selector, where a *lone* `w` only
> surfaces the destination and it takes a second `w` within 3 s to promote OpenCPN to
> live and reach mode 2. Use telnet here and avoid the whole question.

Quit telnet (`^]` then `q`, or just close it) so the logger can have the port.

### T2 · Engage and start logging

1. Close the motor-enable switch. `p` (or the display) → `Navigation: enabled`.
   The display Mode box should read **Waypoint**.
2. Start the logger (§3). Confirm rows are appearing with `sog`, `awa` and `rev/s`.
3. Drive.

### T3 · The straight-road null ★

Drive the long straight road **toward** the waypoint at a steady speed for 3–5
minutes.

With COG ≈ bearing-to-mark, the trim error is ≈ 0, so:

**Pass:** the display's **Target** sits still, close to **Heading**, and does not
wander. `bearing`, `course` and `target` in the log should all track together.

**Fail:** Target drifting steadily on a road pointed at the mark means the trim has a
bias — the thing this test exists to catch.

> **The compass will read wrong in the van, and that is fine.** The 18°
> `COMPASS_MOUNT_OFFSET_DEG` was calibrated to the boat's binnacle; on a bench in a
> van the board sits differently, so Heading is meaningless in absolute terms. The
> track loop should work anyway — it compares bearing-to-mark against COG, both
> true-referenced, and only the *error* crosses into the magnetic-frame
> `heading_command`. **If the loop only behaves when the compass is right, the
> frame-cancelling design is broken.** This is a real test, not a caveat.

### T4 · The sign test ★★ the most valuable thing you can do today

Now drive a road that runs at a **clear angle** to the waypoint — 30–60° off is ideal.

Every 10 s the outer loop applies `0.4 × error`, clamped to 10°.

**Watch the display's Target between ticks:**

- Waypoint off to your **left** (bearing less than course) → Target must step
  **down/left**.
- Waypoint off to your **right** → Target must step **up/right**.
- Steps must be ≤10° and roughly 0.4 × the bearing-vs-course gap.

**A wrong sign here is the single most dangerous bug in the feature** and it cannot be
found on a bench. Note it against the log afterwards if watching while driving is
awkward: the CSV has `bearing`, `course` and `target` in adjacent columns.

### T5 · COG trust gate — free at every traffic light

Stopped or crawling under **0.8 kn**, `cog_damped_valid` drops to 0 and trimming
stops; it re-arms above **1.0 kn** (deliberate hysteresis).

**Pass:** `cog_damped_valid` goes 1 → 0 as you stop, `target` freezes while stopped,
and both resume once moving. This is what stops the loop steering on garbage COG at
low speed, and a stop light exercises it perfectly.

### T6 · GPS fix loss ⚠ not in a parking garage

You have a mast on the roof. **Do not drive into a structure.** Instead, stationary
with the engine running, unplug the GPS antenna for ~30 s and watch `p`.

**Pass:** mode 2 → 1 holding the current heading, **navigation stays enabled**
(`compass_fallback` latched); reconnect the antenna and mode returns to 2 by itself.
If you touch mode or heading while the fix is out, the auto-resume is cancelled — also
by design, so try that variant too.

### T7 · Arrival / circle-the-mark

Drive **past** the waypoint and keep going.

**Pass:** distance bottoms out and grows; bearing swings through ~180°; **mode stays
2** and the controller keeps steering at the mark it has passed. Target will start
trimming toward a U-turn — watch it, obviously do not follow it.

That is the documented end-of-route policy (keep steering the last waypoint rather
than fall back to compass-hold). What is worth recording is the **range at which
bearing starts swinging wildly** — that is the known rough edge, and it is the number
that decides whether Item B in `DeferredWork-PostSeaTrial.md` needs pulling forward.

### T8 · Leg advance

Stop the logger, telnet a new waypoint (`w<lat>,<lon>`), restart the logger, drive on.

**Pass:** bearing and distance jump to the new mark immediately and Target reseeds
without waiting several 10 s ticks (`setWaypoint()` reseeds `heading_command`
precisely so a new leg doesn't crawl).

---

## 6. Part 2 — Wind calibration

**Pick the calmest morning you can.** The tool corrects for ambient wind (see §7), but
calm still gives the best answer and, more importantly, calm air is *steady* air.
Early morning, before thermal activity, on a flat quiet road with **1.5 km usable in
each direction**.

**Use cruise control if the van has it.** It is the single biggest data-quality
improvement available, and it removes the one thing you'd otherwise have to
concentrate on while driving.

### W1 · The run pattern

Drive this, logging continuously. Nothing to type, nothing to mark.

| Leg | Speed | Duration |
|---|---|---|
| 1 | 15 mph | 60 s steady |
| 2 | 15 mph, **reciprocal** | 60 s steady |
| 3 | 25 mph | 60 s |
| 4 | 25 mph reciprocal | 60 s |
| 5 | 35 mph | 60 s |
| 6 | 35 mph reciprocal | 60 s |
| 7 | 45 mph | 60 s |
| 8 | 45 mph reciprocal | 60 s |

**Every speed must be driven in both directions.** Reciprocal pairs are what cancel
the ambient wind — it deflects the vane one way on the outbound leg and the other way
coming back, so the *mean* of a pair is the mounting error alone and the *difference*
is the wind. Without pairs, neither number is trustworthy.

Come to a clear stop (or drop well below the plateau) between legs so the analysis can
separate them. Hold each speed **at least 60 s**; the detector needs 30 s of steady
speed *and* steady heading, so no sweeping bends.

**Add a low-speed pair if you can find an empty lot:** 10 mph in both directions.
The offset term models cup start-up friction, which only shows itself at the bottom
of the range — and light air is exactly where it matters on the boat. Without it you
are extrapolating.

Coverage from this pattern is roughly **13–39 kn apparent**, which brackets most
sailing conditions.

### W2 · Analyse

```bash
python3 roadtest.py fit ~/Desktop/drive-XXXX.csv
```

It finds the plateaus itself, pairs the reciprocals, and prints two ready-to-send
commands. Sample output:

```
  mount error : +6.47 deg   (spread across pairs 0.22)
  SEND        : d-6.5
  ambient     : ~2.0 kn crosswind (max 2.0), estimated from the vane
  fit         : v[m/s] = 1.00866 * n -0.35001   (r2 = 1.0000)
  slope       : 1.1200   (1.0 = nominal Yachta geometry)
  offset      : -0.3500 m/s
  SEND        : k1.1200,-0.3500
```

Read the diagnostics, not just the answers:

- **`spread across pairs`** — the mounting error is a *constant*, so pairs that
  disagree mean the air wasn't steady, a run wasn't straight, or the pole moved.
  Under 0.3° is good; over 1° gets a warning and should be re-run.
- **`ambient`** — the crosswind the vane itself measured. Sanity-check it against
  what the day felt like. If it says 8 kn and you thought it was calm, something is
  wrong with the vane, not the weather.
- **`r2`** — should be 0.999+. This is a straight line through clean data; anything
  less means contaminated plateaus.
- **`slope`** near 1.0 means the nominal Yachta cup geometry was about right for
  this head. Wildly different (say <0.7 or >1.5) suggests a build difference worth
  understanding before trusting it.

### W3 · Send it, then verify

```bash
apcmd 'd-6.5'
apcmd 'k1.1200,-0.3500'
```

Bounds enforced at the board: slope 0.1–10, |offset| ≤ 5 m/s, |trim| ≤ 180°. The
board range-checks before writing flash, and it uses `!(x >= min && x <= max)` on
purpose so a NaN out of a malformed datagram can't get through.

**There is no ack** — `nc` returning tells you nothing, and even telnet's "ok" means
*sent*, not *applied*. Confirm by watching the value change on the next `p`.

Then **drive one more reciprocal pair at 30 mph and re-run the fit.**

**Pass:** the new fit reports mount error ≈ 0 and slope ≈ 1.0 / offset ≈ 0 — i.e. the
residual after your correction is nothing. Indicated wind speed should now match SOG
within a few tenths of a knot on a calm run.

> `d` accumulates rather than replacing (two nudges are two trims), so if you send a
> correction twice you will over-shoot by exactly that amount. `k` replaces.

### W4 · What to leave for the boat

The van gives you the **sensor + mount** error, absolutely. It cannot give you the
rig error: mast rotation relative to the hull, and mainsail upwash at the masthead.
Neither is visible from a van roof and neither is small.

So on the boat, run the tack test from `SeaTrial-WindAndTrack-Plan.md` W2 as
normal — but it should now be a **small** residual trim rather than the whole
correction, which makes it far easier to converge. Same for speed: your handheld
meter gives the final trim, on top of a calibration that is already close.

### W5 · Free bonus — true wind, properly tested

`getTrueWind()` subtracts the boat's own motion from the apparent wind. A van tests
this *better than a boat can*, because you know the answer: drive on a day with a
steady breeze and the derived true wind should come back as **the actual ambient
wind**, no matter how fast you drive.

TWA is measured from the bow, so:

- Drive **north** with a west wind → `True wind` ≈ **270°**, at the real wind speed.
- Turn around and drive **south** → the same wind now reads ≈ **90°**.
- Speed should stay the same on both headings. That invariance *is* the test.

Check `p`'s True wind line on a few headings. It needs a GPS fix (no fix → `no GPS
fix - no boat speed to subtract`), and below 0.8 kn SOG true reads as apparent by
design.

If TWS changes with your heading or your speed, the derivation is wrong — and that
is a bug you would struggle to see on the water, where you never know the true wind
independently.

---

## 7. Why the tool corrects for crosswind (and why you should still pick a calm day)

Reciprocal averaging cancels the **along-road** wind exactly: it is +W outbound and
−W back. It does *not* cancel a **crosswind**, because apparent speed is
`hypot(v ± W_along, W_cross)` — a second-order `W_cross²/(2v)` term survives the
average, and it is worst at low speed, so it tilts the fit and lands almost entirely
on the **offset**: the parameter that matters most in light air.

The vane measures exactly what's needed to remove it. Half the difference in AWA
between a reciprocal pair is the crosswind deflection (the mounting error is common
to both, so it cancels in the difference — this works *before* the vane is
calibrated), and `W_cross = v · tan(deflection)`. The speed the cups actually saw is
then `hypot(v, W_cross)`, not `v`.

Validated against synthetic data with known truth (slope 1.1200, offset −0.3500 m/s,
mount +6.50°):

| Ambient wind | slope | offset | mount |
|---|---|---|---|
| 0 kn | 1.1200 | −0.3502 | +6.52° |
| 2 kn | 1.1200 | −0.3500 | +6.47° |
| 4 kn | 1.1200 | −0.3490 | +6.32° |
| 8 kn | 1.1194 | −0.3378 | +5.80° ⚠ |

**Speed calibration survives real wind.** The **vane offset does not** — its residual
grows with wind because the two deflections are `atan(Wc/(v+Wa))` and
`atan(−Wc/(v−Wa))`, whose magnitudes differ. The tool weights faster pairs more
heavily (they are less contaminated) and warns above 1° of spread, but the honest fix
is a calm morning and the highest safe speeds.

---

## 8. Command cheat sheet

| Want | Command |
|---|---|
| Status | telnet `p` |
| Set waypoint | telnet `w<lat>,<lon>` |
| Mode | telnet `m1` / `m2`, or `apcmd 'm2'`, or the display MODE button |
| Engage navigation | **the motor-enable switch only** |
| Vane trim | `apcmd 'd-6.5'` (accumulates) |
| Speed calibration | `apcmd 'k1.12,-0.35'` (replaces) |
| Vane zero | `v` — **not useful here**; it zeroes the vane's *current* position, so it only makes sense with a hand on the vane at a standstill |
| Start logging | `python3 roadtest.py log --out drive.csv` |
| Analyse | `python3 roadtest.py fit drive.csv` |

Reminder: the logger holds the single telnet slot. Use `apcmd` while it runs.
