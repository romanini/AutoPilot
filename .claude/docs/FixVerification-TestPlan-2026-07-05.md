# Fix-verification test plan — 2026-07-05 session

Verifies the fixes landed on 2026-07-05 and, just as importantly, that the
default build still behaves exactly as before. Everything here is benchable —
no Garmin, no water. Where a fix is compile-time-gated, the default build is
the regression test and the flipped build is the feature test.

## What changed (and what gates it)

| # | Fix | File(s) | Active in default build? |
|---|-----|---------|--------------------------|
| F2 | Display drops `~APRX` frames; unknown-sentence print guarded against USB-detach deadlock | `display/AutoPilot.cpp` | **Yes** |
| F3 | Telnet input clamped to buffer (rejects >99-char lines) | `controller/telnet.ino` | **Yes** |
| F4 | A pending lone Set WP is consumed every tick — can no longer fire hours later and redirect steering | `controller/navsource.ino` | **Yes** |
| F5 | Spinlock around cross-core XTE/BOD state | `controller/crosstrack.ino` | No — only when `XTE_STEERING_ENABLED 1` |
| F6 | Low-speed COG→compass fallback in mode 2 | `controller/controller.ino` | No — only when `COG_FALLBACK_ENABLED 1` (default 0 = old behavior) |
| F8 | `printAutoPilot()` buffer 13→16 | both `AutoPilot.cpp` | Dead code — compile-only |
| F12 | BNO08x rotation vector 500 Hz → 100 Hz (matches consumer) | `controller/compass.ino` | **Yes** |

## Test rig

- **Mac**: builds/flashes both boards over USB; serial monitor for the display
  (the Mac is not on SoberPilot — all telnet/UDP checks run from the Navigator).
- **Navigator (OrangePi)**: `telnet 10.20.1.1` for controller commands and
  `g`-injects; OpenCPN + autopilot_pi for the plugin checks.
- **Display unit**: powered, joined to SoberPilot.
- No GPS fix is needed except in T6 (which needs a fix with the boat/bench
  stationary — the normal bench condition).

**Handy inject lines** (valid checksums, usable verbatim after telnet `g`):

```
g$GPRMB,A,0.10,L,WPT01,WPT02,3742.000,N,12224.000,W,2.5,180.0,5.0,V*28
g$GPRMB,V,,,,,,,,,,,,*30
g$GPXTE,A,A,0.10,L,N*6F
g$GPBOD,180.0,T,,M,WPT02,WPT01*63
```

(Checksums generated with `emulator/nmea.py`; to build new lines:
`python3 -c "import nmea; print(nmea.frame('GPRMB,A,...'))"` from `emulator/`.)

---

## T0 — Build matrix (already green on 2026-07-05, re-run after any tweak)

Compile the controller in three configs and the display once:

1. Controller default (`XTE 0`, `COG_FALLBACK 0`) ✅
2. Controller `XTE_STEERING_ENABLED 1` ✅
3. Controller `COG_FALLBACK_ENABLED 1` ✅
4. Display ✅

**Pass:** all four compile with no errors. Flash the **default** controller
build and the display build for T1–T5.

## T1 — Regression smoke (default build)

The default build's steering logic should be indistinguishable from before the
session (F6 compiled out, F4/F12 are the only live behavior changes).

1. Power controller + display. Display connects, `~APDAT` fields populate.
2. From Navigator: `telnet 10.20.1.1` → `p` prints status; `?` prints help.
3. `n1` then `a10` / `a-10`: bearing moves, motor drives, display tracks.
4. Mode button, adjust buttons, tack on the physical display behave as usual.
5. Leave running 10 min: no reboot, no display disconnect.

**Pass:** everything behaves exactly as the last known-good build.

## T2 — F12: compass rate

1. Telnet `p` repeatedly (or watch the display Heading box) while slowly and
   continuously rotating the controller board.
2. **Pass:** heading tracks the rotation in real time — no growing lag, no
   multi-second stale readings, no "sensor was reset" spam on the controller
   serial log. Pitch/roll/stability still update.

Lag here was the failure mode of the old 500 Hz setting (events queued in the
sensor faster than the loop drained them), so "tracks live" is the whole test.

## T3 — F3: telnet clamp

1. In a telnet session, paste a >100-char line (e.g. 120 × `x`).
   **Pass:** controller replies `-1 Line too long`, does not crash, and the
   session still works (`p` afterward succeeds).
2. Paste a maximum-length legit command: `g` + a full 82-char NMEA sentence.
   **Pass:** accepted (`ok - relayed` or `filtered`, not `Line too long`).

## T4 — F2: display vs `~APRX`

Generate a relay stream with no Garmin: from the Navigator telnet, inject the
RMB line from the rig section every 2–3 s (or loop it with `expect`/a script).
Each accepted inject broadcasts one `~APRX` that every display receives.

1. **Plugged in:** display USB on the Mac, serial monitor open. Inject ~10
   lines. **Pass:** zero `unknown sentence` output; display keeps updating.
2. **Detach under fire:** while injects continue, unplug the display's USB.
   Keep injecting ~1 min. **Pass:** display keeps running — screen updates,
   buttons beep, no freeze/"no link".
3. Replug USB. **Pass:** serial output resumes, display unaffected.

## T5 — F4: stale Set WP cannot redirect

Needs OpenCPN with autopilot_pi and a telnet session (Garmin is simulated by
RMB injects, which drive the real navsource path).

- **A. Pending dropped while a source is live (the bug):**
  1. Keep GARMIN live: inject the RMB `A` line at least every 5 s.
     Confirm Follow shows `GARMIN`, mode 2, waypoint ≈ 37.70, −122.40.
  2. In the plugin, activate some other waypoint and click **Set WP once**
     (Follow checkbox OFF — a single `w`).
  3. Keep the RMB injects going another 10 s, then **stop** them. After ~6 s
     GARMIN goes stale.
  4. **Pass:** telemetry shows `nav_source = NONE` and the waypoint is **still
     the Garmin one** (boat would keep circling the last mark). The lat/lon
     from step 2 must NOT appear. Controller log shows no late
     `setWaypoint` to the OpenCPN position.
- **B. Surface-once still works (regression):**
  1. With nothing live (no injects, Follow off), click **Set WP once**.
  2. **Pass:** within a tick the APDAT waypoint fields show that position;
     mode does not change; nav_source stays NONE.
- **C. Follow still promotes (regression):**
  1. Check **Follow** in the plugin (sustained 1 Hz `w` heartbeat).
  2. **Pass:** within ~3 s nav_source = OPENCPN and mode = 2. Unchecking
     Follow (`X`) clears it immediately.

## T6 — F6: COG fallback flag (both settings)

Needs a GPS fix, bench stationary (speed reads 0).

- **Default build (`COG_FALLBACK_ENABLED 0`) — regression:** with a fix,
  waypoint set (telnet `w<lat>,<lon>`), `m2`, `n1`: mode-2 behavior is the
  OLD behavior — the PID input is the (frozen) GPS course, so rotating the
  board does **not** change the correction. This is the workbench-testable
  path and must match pre-session behavior.
- **Test build (`COG_FALLBACK_ENABLED 1`):** flash it, same setup. With
  speed = 0 the PID input is now the compass heading: rotating the board in
  mode 2 **does** move bearing_correction / drive the motor, same as mode 1.
  Restore the flag to 0 and reflash the default build afterward.

## T7 — F5: XTE build sanity (optional until Item A is enabled)

Flash the `XTE_STEERING_ENABLED 1` build.

1. Set up a live GARMIN mode-2 nav as in T5-A, and add the XTE + BOD injects
   from the rig section after each RMB.
2. **Pass:** setpoint shifts off plain bearing-to-mark (bearing_correction
   changes when XTE injects flip `L`/`R`), and stopping XTE/BOD for >6 s falls
   back to bearing-to-mark.
3. **Soak:** loop RMB+XTE+BOD injects for 10+ min. **Pass:** no watchdog
   reset, no heading/setpoint glitches. (The spinlock race isn't directly
   provokable; a clean soak plus the T0 compile is the practical check.)
4. Reflash the default build when done.

## T8 — F8: no runtime test

`printAutoPilot()` is currently uncalled on both boards; the T0 compiles are
the verification. Nothing to run.

---

## Suggested order & effort

| Test | Needs | Time |
|------|-------|------|
| T0 | Mac | done / 5 min |
| T1 | rig | 15 min |
| T2 | rig | 5 min |
| T3 | telnet | 5 min |
| T4 | rig + display USB | 10 min |
| T5 | rig + OpenCPN | 20 min |
| T6 | GPS fix | 15 min (+ reflash for the =1 half) |
| T7 | optional | 20 min |

T1–T5 on the default build is the core "didn't break anything" pass; T6's
default-build half belongs in it too. T6's flag-on half and T7 are feature
tests you can defer.
