# Wire Contract v2 — FROZEN (Route Communication)

Single source of truth for the UDP/NMEA wire protocol shared by the **controller
firmware**, the **OpenCPN plugin** (`autopilot_pi`), and the **Python Garmin
emulator**. This is step 0 / §1 of [`RouteImplementationPlan.md`](RouteImplementationPlan.md)
("freeze this first"). All three components implement to *this* doc; if one drifts,
the round-trip integration test fails loudly.

Framing is unchanged from v1: every UDP datagram is one ASCII frame, a leading
`~` and a trailing `$`, fields comma-separated, **no CRLF inside the frame**.

Status legend: ✅ unchanged · 🆕 new · ➕ additive change.

---

## 1. UDP message summary

| Message | Dir | Port | Cast | Status |
|---|---|---|---|---|
| `~APDAT,…$` | controller → displays/plugin | 8888 | broadcast (10.20.1.255) | ➕ one field appended |
| `~APCMD,<cmd>$` | display/plugin → controller | 8889 | unicast (10.20.1.1) | ➕ new `X` sub-cmd; `w` now drives liveness |
| `~RESET,1$` | controller → all | 8888 | broadcast | ✅ |
| `~APTX,<nmea>$` | plugin → controller | 8889 | unicast | 🆕 |
| `~APRX,<nmea>$` | controller → all | 8888 | broadcast | 🆕 |

Ports/cast directions are the existing convention (telemetry broadcast on 8888,
commands unicast on 8889). The two new messages reuse those same two channels so
no new sockets are needed: `~APTX` rides the 8889 command channel into the
controller; `~APRX` rides the 8888 telemetry channel back out.

---

## 2. ➕ APDAT: append `nav_source` (one field)

Append **exactly one** integer field, `nav_source`, to the **end** of `~APDAT`,
immediately after `location_lon`. Appending at the tail keeps every existing
field offset unchanged so v1 consumers that stop early still decode correctly.

```
nav_source ∈ { 0 = NONE, 1 = GARMIN, 2 = OPENCPN }   (printed with %d)
```

This is the field count change. v1 carries **25** fields after the `~APDAT,`
tag; v2 carries **26**, the new one last:

```
~APDAT,year,month,day,hour,minute,fix,fixquality,satellites,
       nav_enabled,mode,waypoint_set,wp_lat,wp_lon,
       heading_desired,heading,pitch,roll,stability,
       bearing,bearing_correction,speed,distance,course,
       location_lat,location_lon,nav_source$
       └────────────── 25 existing ───────────────┘└── new ──┘
```

> ⚠️ **Atomic three-parser rule.** APDAT is parsed positionally in three places;
> they MUST land together (see plan §5 for the landing sequence). A standby/extra
> field is explicitly **deferred** — start with this one int only.

| File | Edit |
|---|---|
| `Arduino/controller/publish.ino` | add `,%d` to the format string before `$`; add `autoPilot.getNavSource()` as the last arg (after `getLocationLon()`) |
| `opencpn_plugin/.../include/AutoPilotLink.h` | add `int nav_source;` to `AutoPilotState` |
| `opencpn_plugin/.../src/AutoPilotLink.cpp` `ParsePacket()` | add `s.nav_source = nextInt();` after `s.location_lon = nextDouble();` |
| `Arduino/display/AutoPilot.cpp` `parseAPDAT()` | add one more `advance_field` block after `location_lon`, storing `nav_source` (an `int`) |

Encoding note: `nav_source` is the controller's authoritative view of **who is
currently steering** (the arbitration selector, plan §2.4), not merely which
sources are live. `0` whenever nav is not engaged from a nav source.

---

## 3. 🆕 `~APTX` — plugin writes a raw NMEA line to the Garmin UART

```
~APTX,<one complete NMEA-0183 sentence, including leading '$' and *CRC, NO CRLF>$
```

- **Sender:** plugin, unicast to controller `10.20.1.1:8889`.
- **Receiver:** controller. Strips the `~APTX,` … `$` frame, appends `\r\n`,
  enqueues the inner line onto the Garmin outbound FIFO → written to
  `Serial1Port` (COM1, 4800-8N1).
- The inner payload is a normal NMEA sentence and therefore **itself contains a
  `$`** (the NMEA start delimiter). The controller must locate the frame
  terminator as the **last** `$` of the datagram, not the first. (NMEA also has
  no `~`, so the outer framing is unambiguous on that end.)
- Example datagram (note inner `$…*hh` is the NMEA, outer `~…$` is the frame):
  ```
  ~APTX,$GPWPL,3723.460,N,12158.940,W,WPT01*5E$
  ```

Controller intake: `~APTX` is **not** an `~APCMD` frame. Handle it in
`process_udp_command` *before* the `strncmp(buffer,"~APCMD,",7)` check (or with a
sibling `strncmp(buffer,"~APTX,",6)` branch), since the current code rejects
anything not starting with `~APCMD,`.

---

## 4. 🆕 `~APRX` — controller relays a line it received from the Garmin

```
~APRX,<raw NMEA line exactly as received from the Garmin, no CRLF>$
```

- **Sender:** controller, broadcast on 8888 (so plugin + monitor both see it).
- **Receiver:** plugin (route ingest), `monitor/` (debugging).
- **Relay filter — relay ONLY these sentence types:** `WPL`, `RTE`, `RMB`,
  `XTE`, `BOD`. Drop the high-rate position sentences (`RMC`, `GGA`, `GLL`,
  `VTG`) so the UDP broadcast isn't flooded.
- Match on the 3-letter sentence type **ignoring the talker ID**, i.e. characters
  3–5 of the NMEA address field (`$GPWPL` → `WPL`, `$IIRMB` → `RMB`). Relay the
  line **verbatim** (original talker, original checksum).

---

## 5. ➕ APCMD: two new sub-commands for OpenCPN-as-steering-source

Existing `~APCMD,<cmd>$` dispatch is by `buffer[0]`. Today: `a` adjust, `m` mode,
`n` nav-enable, `w` set-waypoint. Add **one** new command (`X`); `w` is reused as
the OpenCPN active-nav heartbeat:

| Frame | Char | Meaning | Cadence | Status |
|---|---|---|---|---|
| `~APCMD,w<lat>,<lon>$` | `w` | set waypoint **and** refresh OPENCPN-source liveness. Serves both the manual "Set WP" button and the Follow heartbeat — they are intentionally indistinguishable on the wire. | on button press, *and* repeated several times/s while Following | ➕ now drives liveness |
| `~APCMD,X$` | `X` | OpenCPN nav stopped: mark OPENCPN source inactive immediately | once when Following ends | 🆕 |

> **Why no capital `W`** (decided 2026-06-27): the plugin already sends lowercase
> `w` repeatedly while **Follow** is checked (`SetNavigateTarget` →
> `SendWaypoint`, several times/s), so `w` *is* the heartbeat. A separate `W` to
> distinguish the manual one-shot from the active stream was judged redundant.
> Accepted consequence: a one-shot manual "Set WP" click also refreshes OPENCPN
> liveness for one timeout window (~6 s). Harmless while GARMIN is the default
> primary; when OPENCPN is the armed/primary source a stray manual click can
> redirect steering for up to the timeout, and a manual WP differing from a live
> Garmin dest can raise a transient agreement-mismatch flag. Both are acceptable.

- `w` parses lat/lon as today (reentrant `strtok_r`), calls `setWaypoint(lat,lon)`,
  **and** stamps the OPENCPN source `{active=true, last_update_ms=now}` (plan §2.4
  liveness). Mode is unchanged — engaging nav still follows the arming guard / the
  operator pressing Enable (§2.4), so the manual-WP safety pattern is preserved.
- `X` carries no payload; it clears the OPENCPN source (`active=false`) without
  waiting for the ~6 s liveness timeout.
- `dispatch_command` is a `switch(buffer[0])` — `'w'` and `'X'` are the two cases
  touched here.

---

## 6. Shared constants & conventions (so all three agree)

- **NMEA checksum:** XOR of all chars between `$` and `*`, two upper-case hex
  digits, formatted `*HH`. Producers (plugin serialize, emulator) and verifiers
  (controller parse, plugin parse, emulator parse) use the same.
- **Coordinate epsilon for de-dup / agreement (plan §3.3, §2.4):** Garmin emits
  positions as `ddmm.mmm` (~0.001′ ≈ 1.85 m), so round-trips never match
  bit-exactly. Match destinations within an epsilon; **proposed `1.0e-4` degrees
  (~11 m)** — to be confirmed on the bench (plan §7 open Q3/Q4), but all three
  components must use the **same** constant once chosen.
- **Liveness timeout (plan §2.4):** proposed **~6 s** per source; **must match**
  between controller arbitration and the emulator/stub expectations in scenario 4.
- **Frame parsing caution:** because `~APTX` and `~APRX` payloads are raw NMEA
  containing `$`, never use "first `$`" to find the frame end for those two —
  scan to the terminating `$` at the datagram tail. (`~APDAT`/`~APCMD` payloads
  contain no `$`, so the existing first-`$` logic remains fine for them.)

---

## 7. Versioning note

This is **v2** of the wire protocol. v1 = current `main`. The only
backward-incompatible-looking change (APDAT field count) is mitigated by
tail-appending: a v1 parser reading a v2 APDAT simply ignores the trailing
`nav_source`. A v2 parser reading a v1 APDAT gets `0` (NONE) for `nav_source`
from the empty-field guard, which is the correct default. No version byte is
added; the field-append discipline is the compatibility mechanism.
