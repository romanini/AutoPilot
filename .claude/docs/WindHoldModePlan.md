# Mode 3 — Wind-angle hold ("vane mode") implementation plan

*Drafted 2026-09-07. No code changed yet.*

Hold a constant **apparent** wind angle instead of a compass heading or a waypoint,
now that `firmware/Arduino/wind/` is on the wire and the controller already parses,
derives and republishes the masthead data.

---

## 1. The central design decision

**Mode 3 is a second outer loop on the existing cascade, not a new inner loop.**

The 100 Hz inner PID (`control_task`, `controller.ino`) keeps steering by compass
heading. Mode 3 adds a slow outer loop — `windhold.ino`, sitting beside
`gpstracktrim.ino` — that trims `AutoPilot::heading_command` to keep the measured
wind angle on the target.

The payoff is that `control_task` needs **zero changes**. Its setpoint selector is
already:

```c
setpoint = (autoPilot.getMode() == 1) ? autoPilot.getBearing()
                                      : autoPilot.getHeadingCommand();
```

which routes mode 3 to `heading_command` for free. Every property the Option-3
waypoint restructure bought — a rock-steady 100 Hz compass loop, no sensor noise
reaching the rudder, one tuned PID — carries over unchanged.

The alternative (feed AWA straight into `pid_loop()` as the process variable) would
mean a second set of gains, a second autotune, and a 5 Hz noisy signal driving a
100 Hz loop. Don't.

### Where it differs from `gpstracktrim.ino`

Worth stating, because the file will be written from that one:

| | GPS track trim (mode 2) | Wind hold (mode 3) |
|---|---|---|
| Error frame | GPS/true — only the *error* crosses into the magnetic `heading_command`, which is what cancels declination, deviation, leeway and current | Boat frame already. AWA error **is** a heading delta, 1:1, no frame crossing at all |
| Sensor rate | ~1 Hz COG, slow and noisy at low speed | 5 Hz vane, fast but gusty |
| Damping lives | in `AutoPilot` (`cog_sin_avg`/`cog_cos_avg`, fed by `setLocation`) | module-local in `windhold.ino` (see §4) |
| Correct response to a real input change | converge back to the same track | **turn the boat** — that is the whole point of the mode |

### The trim law

AWA arrives 0–360 clockwise from the bow. Signed form `s = normalize180(awa)`,
positive to starboard. With wind bearing `W` and heading `H`, `s = W − H`, so for a
target `s*`:

```
e  = shortestArc(s − s*)          // degrees, + = wind further to starboard than wanted
heading_command += clamp(gain * e, ±clamp_deg)
```

Sign check: target +45, measured +50 → `e = +5` → `H_new = W − 45 = H + 5`. Turn to
starboard by the error. (Same `getCourseCorrection()` helper the class already has.)

---

## 2. Controller changes

### 2.1 `AutoPilot.{h,cpp}` — new state and accessors

New private fields:

```c
float wind_target_angle;   // signed AWA target, -180..180, + = starboard; only meaningful in mode 3
bool  wind_fallback;       // latch: mode 3 auto-dropped to compass hold, resume when wind returns
int   wind_hold_state;     // 0 = inactive, 1 = holding, 2 = frozen/fallback (published, see §2.6)
```

New/changed methods:

- `setMode()` — accept `3`. Entry captures the target and seeds the command:
  `wind_target_angle = signed(wind_direction)`, `heading_command = heading`,
  `wind_fallback = false`. Mirrors what mode 1 entry does with `heading_desired`.
  Clamp/refuse a target outside `[WIND_TARGET_MIN_DEG, WIND_TARGET_MAX_DEG]`
  (see §5) — head-to-wind and dead-downwind are not holdable.
- **The validity gate lives at the caller, not here.** `isWindOk()` is in
  `wind.ino`, not the class, deliberately — it folds in the receive timeout the
  class knows nothing about (same reasoning as `isRudderOk()`). So `setMode(3)`
  cannot check it; `dispatch_command()` and `process_mode()` check `isWindOk()`
  before calling, exactly as the display only offers mode 2 when `waypoint_set`.
- `adjustHeadingDesired()` — currently hard-forces `mode = 1` for any adjust. Add a
  mode-3 branch that adjusts `wind_target_angle` instead and **stays in mode 3**,
  clamped in magnitude and **forbidden from changing sign** (§5). This is what makes
  the existing ±1/±10 buttons trim the wind angle, with no new verb and no sequence
  of presses able to gybe the boat.
- `applyWindHoldTrim(float error, float gain, float clamp_deg)` — new, mirroring
  `applyHeadingCommandTrim()`: does the wrap math and clamping inside the class,
  under the lock, so `windhold.ino` never touches `heading_command` directly. Also
  sets `bearing = heading_command` and
  `bearing_correction = getCourseCorrection(bearing, heading)` so the existing
  Bearing/Correction boxes on every display stay meaningful in mode 3 (mode 3 has no
  destination, so "the compass course we are currently steering" is the honest thing
  to put there).
- `setWindHoldFallback(bool)` / `getWindHoldState()` / `getWindTarget()` /
  `setWindTarget()` — plain locked accessors.
- `setNavigationEnabled()` — the re-engage path currently re-seeds `heading_desired`
  when `mode == 1`. Add a `mode == 3` branch that re-captures the current AWA as the
  target and reseeds `heading_command = heading`, so flipping the motor-enable
  switch back on doesn't resume against a stale target.
- `printAutoPilot()` — the `Destination:` line needs a mode-3 branch
  (`wind +45 AWA (holding)`).

### 2.2 `windhold.ino` — new file

Written from `gpstracktrim.ino`. Two jobs, both called from `command_task`:

**a. `wind_hold_filter_step()`** — first-order IIR on `sin`/`cos` of the raw AWA,
stepped every `command_task` tick (100 ms) with measured `dt`. Vector form is
mandatory for the same reason `cog_damped` uses it and the wind-display's filter
does: arithmetic averaging of 359° and 1° gives 180°.

Module-local state, **not** in `AutoPilot`, and **not published**. The apparent wind
on `~APDAT` stays raw so the wind-display's own 350 ms filter and
`getTrueWind()` are not double-damped.

τ ≈ 4–6 s: slower than gusts and mast whip, faster than a persistent shift.

**b. `wind_hold_tick()`** — the trim, on `WIND_HOLD_PERIOD_MS`. Early-returns unless
`getMode() == 3 && isNavigationEndabled()`. Then, in order:

1. **Health / fallback checks** (§3). Any trip → fall back and return.
2. `e = shortestArc(damped_signed_awa − wind_target_angle)`.
3. Deadband: `|e| < WIND_HOLD_DEADBAND_DEG` → do nothing.
4. `autoPilot.applyWindHoldTrim(e, WIND_HOLD_GAIN, WIND_HOLD_CLAMP_DEG)`.

Also owns the **resume** side of the fallback latch (§3).

### 2.3 `controller.ino`

One line in `command_task`, next to `gps_track_trim()`:

```c
wind_hold_filter_step();
wind_hold_tick();
```

`control_task` unchanged (§1).

### 2.4 `subscribe.ino` — UDP command surface

`case 'm'` currently validates `new_mode >= 1 && new_mode <= 2`. Extend to 3, gated:

```c
if (new_mode == 3 && !isWindOk()) break;   // no vane, no wind mode
```

Plus a forward declaration of `bool isWindOk();` at the top with the other
cross-file decls.

### 2.5 `telnet.ino` — the *other* command surface

Both dispatchers have to be wired up separately — that split is deliberate and has
bitten before. Changes:

- `process_mode()` — same `1..3` range and the same `isWindOk()` gate, with a
  distinct failure message ("no wind data — check the masthead board") so the
  operator isn't sent hunting for a missing waypoint.
- `process_print()` — add a wind-hold line: target, damped AWA, error, hold state,
  and why it is frozen if it is. This is the only place the operator can see the
  filter and the latch, so it earns real detail.
- `process_help()` — update the `m` line to `m<1|2|3>`.
- `process_quit()`'s status echo — mode-3 branch on the `Destination:` line.

### 2.6 `publish.ino` — two appended fields

```
... ,<air_temp_c>,<temp_ok>,<wind_target>,<wind_hold_state>$
```

39 → **41 fields**. Appended only, per the standing convention.

- `wind_target` — `%.1f`, signed −180..180; 0 outside mode 3.
- `wind_hold_state` — `%d`, 0 inactive / 1 holding / 2 frozen-or-fallback. Needed
  because the fallback drops `mode` to 1, so mode alone cannot distinguish
  "operator chose compass" from "wind mode dropped out and is waiting to resume".

Also fix field 14's selector, which currently reads:

```c
(autoPilot.getMode() == 2) ? getHeadingCommand() : getHeadingDesired()
```

→ `(autoPilot.getMode() == 1) ? getHeadingDesired() : getHeadingCommand()`, since
mode 3 steers `heading_command` too.

Buffer sizing: worst case grows ~10 chars (208 → ~218) against `DATA_SIZE` 400. No
bump needed, but **grow every receiver's buffer before the sender's** if that ever
changes — an oversized datagram is dropped whole, so a lagging receiver loses the
entire frame and reads as "not connected", not as "missing fields".

### 2.7 `navsource.ino` — one guard, and it is a real decision

`navsource_tick()` calls `autoPilot.setMode(2)` whenever the selected source
switches. Today that can silently take the boat out of mode 3 because a Garmin route
woke up or the OpenCPN plugin's `w` heartbeat promoted.

**Recommendation:** guard it with `getMode() != 3`. Wind hold is an explicit operator
choice made at the helm; a chart plotter deciding to override it is the same class of
surprise the retired `n` verb was removed to prevent. The source still becomes live
and is still announced in `nav_source` — it just doesn't seize the wheel.

### 2.8 Not changed

`pid.ino`, `motor.ino`, `motorenable.ino`, `compass.ino`, `gps.ino`, `garmin.ino`,
`autotune.ino`, `rudder.ino`, `wind.ino`. And **neither sensor board changes at
all** — `firmware/Arduino/wind/` already sends everything mode 3 needs.

---

## 3. Failure handling — reuse the `setFix` pattern exactly

`setFix()` already does this for GPS: on fix loss, mode 2 → 1 holding current
heading, navigation stays engaged, `compass_fallback` latches, and the fix returning
resumes mode 2 unless the operator intervened in the meantime (any explicit
`setMode`/adjust clears the latch).

Mirror it with `wind_fallback`, driven from `wind_hold_tick()` rather than a setter,
because wind health is a *timeout* computed in `wind.ino`, not a pushed value.

**Trips (mode 3 → mode 1, `wind_hold_state = 2`):**

1. `!isWindOk()` — board quiet, or vane magnet gone.
2. Apparent speed below `WIND_HOLD_MIN_APPARENT_KN` — the vane is meaningless and
   there is no wind to hold anyway.
3. `|e|` beyond `WIND_HOLD_MAX_ERROR_DEG`, sustained for a couple of ticks. That is
   a shift or a wrap, not a hold.
4. **The hemisphere latch, which is the safety item.** The measured AWA leaving the
   target's side — crossing the bow (0°) or the stern (180°). Nulling an error that
   has wrapped past either commands a full tack or, far worse, a gybe. Freeze and
   fall back; **never steer through**.

**Resume:** wind healthy again, above the speed floor, error back inside the band,
same hemisphere, and `wind_fallback` still set (i.e. the operator hasn't touched
anything). Re-capture nothing — resume against the *original* target, which is the
point of latching it.

A Wi-Fi burp at the masthead should be a non-event, exactly as a GPS dropout is now.

---

## 4. Constants (all `#define` in `windhold.ino`, to be tuned on the water)

| Constant | Proposed | Reasoning |
|---|---|---|
| `WIND_HOLD_FILTER_TAU_MS` | 5000 | Slower than gusts/mast whip, faster than a real shift. Contrast the wind-display's 350 ms, which is capped by its 1 Hz packet rate and is a *display* smoother |
| `WIND_HOLD_PERIOD_MS` | 2000 | Long enough that the inner PID settles between trims, same principle as the 10 s GPS trim but far shorter — the vane is a fast sensor |
| `WIND_HOLD_GAIN` | 0.5 | Fraction of the observed error applied per tick |
| `WIND_HOLD_CLAMP_DEG` | 5.0 | Max `heading_command` move per tick |
| `WIND_HOLD_DEADBAND_DEG` | 3.0 | Below the vane's real accuracy once damped; stops the wheel hunting in a steady breeze |
| `WIND_HOLD_MIN_APPARENT_KN` | 3.0 | Vane unreliable below this |
| `WIND_HOLD_MAX_ERROR_DEG` | 45.0 | Beyond this it is a shift, not a hold |
| `WIND_TARGET_MIN_DEG` / `MAX_DEG` | 25 / 170 | Magnitude bounds on the target: no head-to-wind, no dead run. Boat properties, like `NOGO_HALF_ANGLE` in `dial.h` |

---

## 5. Adjust and tack semantics

**Adjust (`a<delta>`) in mode 3 trims the wind angle**, does not demote to mode 1,
and cannot cross the bow or the stern. So ±1° / ±10° on the head unit and the plugin
do the natural thing with no new verb.

**Tack is deliberately out of Phase 1.** The display's TACK button sends `a90`
(`ADJUSTMENT_AMOUNT_TACK`), indistinguishable on the wire from any other adjust. In
mode 3 that would take a +45 target to +135 — a bear-away onto a broad reach, not a
tack. Actively dangerous.

- **Phase 1:** the display disables TACK while `mode == 3` (`button_pressed()`
  already has a per-button gate to hang this on).
- **Phase 2:** new verb — `y`, "mirror the wind target across the bow" — on **both**
  command surfaces, plus the display's tack path. Verb choice: `a m n w X t z v d k`
  are taken on the UDP side and `g p q s ?` additionally on telnet; `dispatch_command()`
  switches on `buffer[0]` alone, so it must be a free single character, and `y` is
  free and unambiguous. Same reasoning that picked `z` for the rudder.

---

## 6. Display (`firmware/Arduino/display/`)

- **`AutoPilot.{h,cpp}`** — store the two new trailing fields; extend `parseAPDAT`'s
  39 blocks by 2. **Placement inside the parser matters:** `wind_target` is
  operator-controlled, so it goes **inside** the `suppressLocalFields` guard
  alongside `heading_desired`; `wind_hold_state` is controller-derived, so it goes
  **outside**, like `nav_enabled`. `setMode()` accepts 3 (mirroring the controller's
  entry logic optimistically); `adjustHeadingDesired()` gets the same mode-3 branch
  so the local optimistic update matches what the controller will do.
- **`screen.ino`** — `display_mode()` gains a `cur_mode == 3` branch: `"Wind"`, or
  `"Wind*"`/`"Wind (hold)"` when `wind_hold_state == 2` so a silent fallback is
  visible. The Target box already shows the right thing (it renders field 14, which
  §2.6 fixes for mode 3). Showing the target AWA itself wants a box — the rudder box
  added in the last layout pass is the model; nothing on this screen renders wind
  yet, so this is the first wind pixel on the head unit.
- **`button.ino`** — MODE cycles 1 → 2 (if `waypoint_set`) → 3 (if `wind_ok`) → 1;
  TACK gated off in mode 3 (§5); the PORT/STARBORD handlers' `if (getMode() == 2)
  set_mode(1)` demotion is already mode-2-specific, so it correctly leaves mode 3
  alone — verify, don't "tidy".
- **`command.ino`** — no change; `set_mode(3)` rides the existing `m` verb.

## 7. Wind display (`firmware/Arduino/wind-display/`) — optional, Phase 3

The natural home for a **target bug** on the dial: a small marker on the ring at the
target AWA, so the helm sees the held angle against the live pointer.

Two constraints from that screen's design: `APDAT_FIELD_COUNT` goes 39 → 41 and the
two fields get `FIELD_*` defines (the indexed walk means adding a field is storing
it, not changing the walk); and a bug drawn **on the ring** is drawn once and moved
rarely, whereas one inside the ring joins the composited interior and the dirty-rect
scheme. On the ring is much cheaper. `./render.sh` after any `dial.ino` change.

## 8. OpenCPN plugin (`autopilot_pi`)

- **`AutoPilotLink.{h,cpp}`** — two fields on `AutoPilotState` (order must mirror
  `~APDAT` exactly), parse them in `ParseApdat()`, and `SendMode()` accepts 3
  (its optimistic branch needs a mode-3 arm alongside the existing 1 and 2).
- **`AutoPilotPanel.cpp`** — `"Wind"` in the mode label next to the existing
  `Compass`/`Waypoint`/`Garmin`/`OpenCPN` cases, and `OnMode()`'s cycle
  `(s.mode == 2) ? 1 : (s.waypoint_set ? 2 : 1)` extended to reach 3. Note the mode
  label and the button exist in **all three dock layouts** (float/dock/compact) —
  three `BuildUI` variants to touch.

## 9. Docs

`.claude/skills/autopilot/SKILL.md` (the mode table says `0=off, 1=compass-hold,
2=waypoint navigate`, the `~APDAT` field list, and the command-surface table), and
`firmware/Arduino/README.md` if it enumerates modes.

---

## 10. Suggested order

1. Controller: `AutoPilot` state + `windhold.ino` + `command_task` wiring + telnet
   `m`/`p`. Nothing on the wire yet — drive it entirely from telnet.
2. Bench test: navigation **disabled**, boat stationary, spin the vane by hand,
   watch `heading_command` and the hold state track on telnet `p`. Every fallback
   trip in §3 is reachable this way (pull the wind board's power for #1, stop the
   cups for #2, swing the vane across the bow for #4).
3. `publish.ino` fields + `subscribe.ino` gate.
4. Receivers, in this order: display parser → display screen/buttons → plugin.
   Parsers before renderers.
5. Dock test with the engine off and the sails down: confirm mode 3 is refusable
   with no wind data, that the fallback trips and resumes, and that the motor-enable
   switch behaves in mode 3.
6. On-water tuning of §4, upwind first. The constants should come from the W6 data
   run in [`SeaTrial-WindAndTrack-Plan.md`](SeaTrial-WindAndTrack-Plan.md) rather
   than from the guesses in the table — and W1/W2/W4 in that same plan are what
   decide whether this mode is worth building at all.
7. Phase 2 (tack verb `y`) and Phase 3 (dial bug) after that.

**Sequencing note:** nothing else in flight collides with this. Nav-engagement
Phase C is still pending but lives entirely inside the plugin's
`AutoPilotLink::FlushInboundRoute()` (inbound Garmin route de-dup/activate) — it
touches no controller file, so it can land before, after or alongside mode 3. The
deferred cross-track and arrival items (`DeferredWork-PostSeaTrial.md`) are gated on
the sea trial, and cross-track is already in-tree behind `XTE_STEERING_ENABLED 0`;
mode 3 doesn't touch `crosstrack.ino` or the `gps_track_trim()` path it hooks.

The one genuine overlap is `navsource_tick()` (§2.7), and that is a one-line guard
in a file Phase C does not open.
