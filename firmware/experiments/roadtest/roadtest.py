#!/usr/bin/env python3
"""Road-test logger and wind-calibration fitter.

Two subcommands:

  log   Unattended 1 Hz logger. Joins two data sources at once and writes one
        CSV row per second:
          * UDP 8888  - the broadcast ~APDAT frame (39 fields). Everything the
            track test needs, including field 14 (the live steering setpoint,
            "Target") which telnet 'p' does not print.
          * telnet 23 - polls 'p' once a second, purely for the anemometer's
            raw rev/s, which is NOT on ~APDAT (it is calibration data, so it
            stays controller-side on the 'p' line).
        Either source may be absent; columns from a missing source come out
        blank rather than stopping the log.

  fit   Reads a log CSV, finds steady-speed plateaus on its own, pairs them
        into reciprocal (out-and-back) runs, and prints the two calibration
        commands to send. No interaction needed while driving - hold each
        speed steady and the plateaus fall out afterwards.

Stdlib only (telnetlib is gone in 3.13, so the telnet side is a plain socket).

  python3 roadtest.py log  --host 10.20.1.1 --out drive.csv
  python3 roadtest.py fit  drive.csv
"""

import argparse
import csv
import math
import re
import socket
import sys
import threading
import time

# ---------------------------------------------------------------------------
# Physical constant, must match firmware/Arduino/wind/Wind.h and Wind.cpp:
#   raw_mps   = (2 * pi * n * ANEMOMETER_RADIUS_M) / ANEMOMETER_LAMBDA
#   speed_mps = raw_mps * slope + offset
# so a fit of reference speed against n gives A = RAW_MPS_PER_REV * slope and
# B = offset. r = 0.043 m, lambda = 0.3.
# ---------------------------------------------------------------------------
RAW_MPS_PER_REV = 2.0 * math.pi * 0.043 / 0.3   # 0.900590 m/s per rev/s

KN_PER_MPS = 1.94384

# ~APDAT field names, in wire order (1-indexed on the wire; 0-indexed here).
# Fields are only ever appended, so a longer frame is fine - extras are ignored.
APDAT_FIELDS = [
    "year", "month", "day", "hour", "minute",
    "fix", "fixquality", "satellites",
    "nav_enabled", "mode", "waypoint_set", "wp_lat", "wp_lon",
    "target",            # field 14: heading_desired (mode 1) or heading_command (mode 2)
    "heading", "pitch", "roll", "stability",
    "bearing", "bearing_correction",
    "sog_kn", "distance", "course",
    "location_lat", "location_lon",
    "nav_source", "autotune_state",
    "cog_damped", "cog_damped_valid",
    "rudder_angle", "rudder_ok",
    "awa", "aws_kn", "wind_ok",
    "twa", "tws_kn", "true_wind_ok",
    "air_temp_c", "temp_ok",
]

CSV_COLUMNS = ["t", "wall"] + APDAT_FIELDS + ["rev_s"]

RE_RAW_REV = re.compile(r"raw\s+([0-9]*\.?[0-9]+)\s+rev/s")


# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

class Latest:
    """Newest value from each source, guarded by a lock."""

    def __init__(self):
        self.lock = threading.Lock()
        self.apdat = {}
        self.rev_s = None
        self.apdat_count = 0
        self.telnet_count = 0

    def set_apdat(self, row):
        with self.lock:
            self.apdat = row
            self.apdat_count += 1

    def set_rev(self, value):
        with self.lock:
            self.rev_s = value
            self.telnet_count += 1

    def snapshot(self):
        with self.lock:
            return dict(self.apdat), self.rev_s


def parse_apdat(line):
    """Return a dict of ~APDAT fields, or None if this isn't a usable frame."""
    line = line.strip()
    if not line.startswith("~APDAT,") or not line.endswith("$"):
        return None
    parts = line[len("~APDAT,"):-1].split(",")
    row = {}
    for name, value in zip(APDAT_FIELDS, parts):
        row[name] = value
    return row


def apdat_reader(latest, stop, port=8888):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.settimeout(1.0)
    try:
        sock.bind(("", port))
    except OSError as exc:
        print("apdat: cannot bind udp %d: %s" % (port, exc), file=sys.stderr)
        return
    while not stop.is_set():
        try:
            data, _ = sock.recvfrom(2048)
        except socket.timeout:
            continue
        except OSError:
            break
        row = parse_apdat(data.decode("ascii", "replace"))
        if row:
            latest.set_apdat(row)
    sock.close()


def strip_telnet_iac(data):
    """Drop any IAC negotiation bytes. ESPTelnet doesn't normally negotiate,
    but a stray 0xFF sequence would otherwise corrupt the line buffer."""
    out = bytearray()
    i = 0
    while i < len(data):
        if data[i] == 0xFF and i + 1 < len(data):
            # IAC + command (+ option for DO/DONT/WILL/WONT)
            i += 3 if data[i + 1] in (0xFB, 0xFC, 0xFD, 0xFE) else 2
            continue
        out.append(data[i])
        i += 1
    return bytes(out)


def telnet_reader(latest, stop, host, port=23, period=1.0):
    """Poll 'p' once a second and pull the raw rev/s out of the Wind line.

    Note the controller's telnet server takes ONE client. While this is running
    you cannot open an interactive session - send commands over UDP ~APCMD
    instead (see send_command below).
    """
    while not stop.is_set():
        sock = None
        try:
            sock = socket.create_connection((host, port), timeout=5.0)
            sock.settimeout(1.0)
            buf = b""
            next_poll = 0.0
            while not stop.is_set():
                now = time.monotonic()
                if now >= next_poll:
                    sock.sendall(b"p\r\n")
                    next_poll = now + period
                try:
                    chunk = sock.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                buf += strip_telnet_iac(chunk)
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode("ascii", "replace")
                    match = RE_RAW_REV.search(text)
                    if match:
                        latest.set_rev(float(match.group(1)))
                    elif text.startswith("Wind:") and "no data" in text:
                        latest.set_rev(None)
        except OSError as exc:
            if not stop.is_set():
                print("telnet: %s (retrying)" % exc, file=sys.stderr)
        finally:
            if sock:
                try:
                    sock.close()
                except OSError:
                    pass
        for _ in range(30):           # ~3 s backoff, interruptible
            if stop.is_set():
                break
            time.sleep(0.1)


def cmd_log(args):
    latest = Latest()
    stop = threading.Event()
    threads = [
        threading.Thread(target=apdat_reader, args=(latest, stop), daemon=True),
        threading.Thread(target=telnet_reader, args=(latest, stop, args.host),
                         daemon=True),
    ]
    for thread in threads:
        thread.start()

    started = time.monotonic()
    rows = 0
    print("logging to %s - Ctrl-C to stop" % args.out)
    try:
        with open(args.out, "w", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=CSV_COLUMNS)
            writer.writeheader()
            while True:
                time.sleep(1.0)
                apdat, rev = latest.snapshot()
                row = {"t": "%.1f" % (time.monotonic() - started),
                       "wall": time.strftime("%H:%M:%S")}
                row.update(apdat)
                row["rev_s"] = "" if rev is None else "%.3f" % rev
                writer.writerow(row)
                handle.flush()
                rows += 1
                if rows % 15 == 0:
                    sog = apdat.get("sog_kn", "?")
                    awa = apdat.get("awa", "?")
                    print("  %s  rows=%d  sog=%s kn  awa=%s  rev/s=%s"
                          % (row["wall"], rows, sog, awa, row["rev_s"] or "-"))
    except KeyboardInterrupt:
        print("\nstopped: %d rows, %d APDAT frames, %d telnet polls"
              % (rows, latest.apdat_count, latest.telnet_count))
    finally:
        stop.set()


# ---------------------------------------------------------------------------
# Fitting
# ---------------------------------------------------------------------------

def signed_angle(deg):
    """0..360 clockwise-from-bow -> -180..180, starboard positive."""
    value = deg % 360.0
    return value - 360.0 if value > 180.0 else value


def heading_delta(a, b):
    """Shortest signed difference a - b, in -180..180."""
    return signed_angle(a - b)


def mean_angle(values):
    """Vector mean of signed angles - averaging 179 and -179 arithmetically
    would give 0, which is the opposite of the answer."""
    if not values:
        return 0.0
    sin_sum = sum(math.sin(math.radians(v)) for v in values)
    cos_sum = sum(math.cos(math.radians(v)) for v in values)
    return math.degrees(math.atan2(sin_sum / len(values), cos_sum / len(values)))


def read_log(path):
    rows = []
    with open(path, newline="") as handle:
        for raw in csv.DictReader(handle):
            try:
                sog = float(raw["sog_kn"])
                heading = float(raw["heading"])
                awa = float(raw["awa"])
            except (TypeError, ValueError, KeyError):
                continue
            rev = raw.get("rev_s") or ""
            try:
                rev_value = float(rev)
            except ValueError:
                rev_value = None
            rows.append({
                "t": float(raw.get("t") or 0.0),
                "wall": raw.get("wall", ""),
                "sog": sog,
                "heading": heading,
                "awa": signed_angle(awa),
                "rev": rev_value,
                "wind_ok": (raw.get("wind_ok") or "0").strip() == "1",
            })
    return rows


def find_plateaus(rows, min_len, min_sog, sog_tol, head_tol):
    """Greedy scan for runs that are steady in BOTH speed and heading.

    Heading matters as much as speed: a constant-speed sweeping bend still
    changes the apparent wind angle, and would poison a vane-offset average.
    """
    plateaus = []
    index = 0
    while index < len(rows):
        if rows[index]["sog"] < min_sog or not rows[index]["wind_ok"]:
            index += 1
            continue
        run = [rows[index]]
        cursor = index + 1
        while cursor < len(rows):
            candidate = rows[cursor]
            sogs = [r["sog"] for r in run] + [candidate["sog"]]
            if max(sogs) - min(sogs) > sog_tol:
                break
            if abs(heading_delta(candidate["heading"], run[0]["heading"])) > head_tol:
                break
            if not candidate["wind_ok"] or candidate["sog"] < min_sog:
                break
            run.append(candidate)
            cursor += 1
        if len(run) >= min_len:
            revs = [r["rev"] for r in run if r["rev"] is not None]
            plateaus.append({
                "start": run[0]["wall"],
                "n": len(run),
                "sog": sum(r["sog"] for r in run) / len(run),
                "heading": mean_angle([r["heading"] for r in run]) % 360.0,
                "awa": mean_angle([r["awa"] for r in run]),
                "rev": (sum(revs) / len(revs)) if revs else None,
                "rev_n": len(revs),
            })
            index = cursor
        else:
            index += 1
    return plateaus


def pair_reciprocals(plateaus, speed_tol, recip_tol):
    """Match each plateau with an opposite-heading one at a similar speed.

    Reciprocal pairing is what cancels a steady ambient wind: driving into a
    crosswind deflects the vane one way and the return leg deflects it the
    other by the same amount, so the mean of the pair is the mounting error
    alone. Same for speed - a headwind one way is a tailwind back.
    """
    pairs = []
    used = set()
    for i, first in enumerate(plateaus):
        if i in used:
            continue
        best, best_score = None, None
        for j, second in enumerate(plateaus):
            if j == i or j in used:
                continue
            reciprocal = abs(abs(heading_delta(first["heading"], second["heading"])) - 180.0)
            speed_gap = abs(first["sog"] - second["sog"])
            if reciprocal > recip_tol or speed_gap > speed_tol:
                continue
            score = reciprocal + speed_gap * 10.0
            if best_score is None or score < best_score:
                best, best_score = j, score
        if best is not None:
            used.add(i)
            used.add(best)
            pairs.append((first, plateaus[best]))
    unpaired = [p for k, p in enumerate(plateaus) if k not in used]
    return pairs, unpaired


def least_squares(points):
    """Ordinary least squares y = A*x + B. Returns (A, B, r2)."""
    count = len(points)
    if count < 2:
        return None
    mean_x = sum(x for x, _ in points) / count
    mean_y = sum(y for _, y in points) / count
    sxx = sum((x - mean_x) ** 2 for x, _ in points)
    if sxx == 0:
        return None
    sxy = sum((x - mean_x) * (y - mean_y) for x, y in points)
    slope_a = sxy / sxx
    intercept_b = mean_y - slope_a * mean_x
    ss_tot = sum((y - mean_y) ** 2 for _, y in points)
    ss_res = sum((y - (slope_a * x + intercept_b)) ** 2 for x, y in points)
    r2 = 1.0 - ss_res / ss_tot if ss_tot > 0 else float("nan")
    return slope_a, intercept_b, r2


def cmd_fit(args):
    rows = read_log(args.log)
    if not rows:
        print("no usable rows in %s "
              "(needs sog_kn, heading and awa columns populated)" % args.log)
        return 1
    print("%d usable samples over %.1f min\n" % (len(rows), (rows[-1]["t"] - rows[0]["t"]) / 60.0))

    plateaus = find_plateaus(rows, args.min_len, args.min_sog,
                             args.sog_tol, args.head_tol)
    if not plateaus:
        print("no steady plateaus found. Need >=%ds of steady speed (+/-%.1f kn) "
              "and steady heading (+/-%.0f deg) above %.1f kn."
              % (args.min_len, args.sog_tol, args.head_tol, args.min_sog))
        return 1

    print("Steady plateaus")
    print("  %-9s %5s %7s %8s %8s %9s" % ("start", "secs", "sog_kn", "hdg", "awa", "rev/s"))
    for plateau in plateaus:
        print("  %-9s %5d %7.2f %8.1f %8.2f %9s"
              % (plateau["start"], plateau["n"], plateau["sog"], plateau["heading"],
                 plateau["awa"],
                 "-" if plateau["rev"] is None else "%.3f" % plateau["rev"]))

    pairs, unpaired = pair_reciprocals(plateaus, args.speed_tol, args.recip_tol)
    print("\n%d reciprocal pair(s), %d unpaired" % (len(pairs), len(unpaired)))

    # ---- vane offset -----------------------------------------------------
    print("\n--- Vane alignment ---")
    if pairs:
        errors = [(a["awa"] + b["awa"]) / 2.0 for a, b in pairs]
        crosswinds = [(a["awa"] - b["awa"]) / 2.0 for a, b in pairs]
        # Averaging the pair cancels the mounting error's crosswind
        # contamination only to first order: the two deflections are
        # atan(Wc / (v + Wa)) and atan(-Wc / (v - Wa)), whose magnitudes differ
        # because the along-road wind changes each denominator. The residual
        # falls off with speed, so weight faster pairs more heavily - they are
        # measurably closer to the truth (visible in the per-pair column below,
        # which trends toward the real value as speed rises).
        weights = [((a["sog"] + b["sog"]) / 2.0) ** 2 for a, b in pairs]
        offset = sum(e * w for e, w in zip(errors, weights)) / sum(weights)
        spread = max(errors) - min(errors) if len(errors) > 1 else 0.0
        for (a, b), err, cross in zip(pairs, errors, crosswinds):
            print("  %5.1f kn  %5.0f/%5.0f deg -> awa %+6.2f / %+6.2f"
                  "  => mount %+6.2f, ambient %+6.2f"
                  % (a["sog"], a["heading"], b["heading"], a["awa"], b["awa"], err, cross))
        print("\n  mount error : %+.2f deg   (spread across pairs %.2f)" % (offset, spread))
        print("  SEND        : d%+.1f" % (-offset))
        if spread > 1.0:
            print("  WARNING: pairs disagree by more than 1 deg. The mount error is a")
            print("           constant, so a spread means the air was not steady (or the")
            print("           runs were not straight, or the pole moved). Re-run calmer.")
    else:
        raw_offset = mean_angle([p["awa"] for p in plateaus])
        print("  No reciprocal pairs, so ambient wind cannot be cancelled.")
        print("  Uncorrected mean AWA: %+.2f deg  ->  d%+.1f (LESS RELIABLE)"
              % (raw_offset, -raw_offset))

    # ---- speed calibration ----------------------------------------------
    print("\n--- Speed calibration ---")
    points = []
    crosswind_kn = []
    if pairs:
        for a, b in pairs:
            if a["rev"] is None or b["rev"] is None:
                continue
            van_kn = (a["sog"] + b["sog"]) / 2.0
            # Reciprocal averaging cancels the ALONG-road wind exactly (it is
            # +W one way and -W back), but not the crosswind: apparent speed is
            # hypot(v +/- W_along, W_cross), so a second-order W_cross^2/(2v)
            # term survives the average and is worst at low speed - which tilts
            # the fit and lands almost entirely on the offset, the parameter
            # that matters most in light air.
            #
            # The vane measures exactly what is needed to remove it. Half the
            # difference in AWA between the two runs is the crosswind
            # deflection (the mounting error is common to both, so it cancels
            # in the difference - this works before the vane is calibrated),
            # and W_cross = v * tan(deflection). The speed the cups actually
            # saw is then hypot(v, W_cross), not v.
            deflection = (a["awa"] - b["awa"]) / 2.0
            w_cross = van_kn * math.tan(math.radians(deflection))
            crosswind_kn.append(abs(w_cross))
            ref_mps = math.hypot(van_kn, w_cross) / KN_PER_MPS
            points.append(((a["rev"] + b["rev"]) / 2.0, ref_mps))
        source = "reciprocal pairs, crosswind-corrected"
    if not points:
        points = [(p["rev"], p["sog"] / KN_PER_MPS)
                  for p in plateaus if p["rev"] is not None]
        source = "unpaired plateaus (ambient wind NOT cancelled)"

    if len(points) < 2:
        print("  Need at least 2 speed points with rev/s. Was the telnet logger")
        print("  connected? rev/s is only on the 'p' line, never on ~APDAT.")
        return 1

    fit = least_squares(points)
    if fit is None:
        print("  All points at the same rev/s - vary the speed.")
        return 1
    grad, intercept, r2 = fit
    slope = grad / RAW_MPS_PER_REV
    print("  %d point(s) from %s" % (len(points), source))
    if crosswind_kn:
        print("  ambient     : ~%.1f kn crosswind (max %.1f), estimated from the vane"
              % (sum(crosswind_kn) / len(crosswind_kn), max(crosswind_kn)))
    print("  speed range : %.1f - %.1f kn apparent"
          % (min(y for _, y in points) * KN_PER_MPS,
             max(y for _, y in points) * KN_PER_MPS))
    print("  fit         : v[m/s] = %.5f * n %+.5f   (r2 = %.4f)" % (grad, intercept, r2))
    print("  slope       : %.4f   (1.0 = nominal Yachta geometry)" % slope)
    print("  offset      : %+.4f m/s" % intercept)
    print("  SEND        : k%.4f,%.4f" % (slope, intercept))

    problems = []
    if not (0.1 <= slope <= 10.0):
        problems.append("slope %.4f is outside the board's accepted 0.1..10" % slope)
    if abs(intercept) > 5.0:
        problems.append("offset %.4f exceeds the board's +/-5 m/s limit" % intercept)
    if r2 < 0.99:
        problems.append("r2 %.4f is low for what should be a clean straight line" % r2)
    if len(points) < 4:
        problems.append("only %d point(s) - 5 or 6 speeds makes a much better fit" % len(points))
    if problems:
        print("\n  CHECK:")
        for problem in problems:
            print("    - %s" % problem)
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    subparsers = parser.add_subparsers(dest="command")

    log_parser = subparsers.add_parser("log", help="unattended 1 Hz logger")
    log_parser.add_argument("--host", default="10.20.1.1", help="controller IP")
    log_parser.add_argument("--out", default="roadtest.csv", help="output CSV")
    log_parser.set_defaults(func=cmd_log)

    fit_parser = subparsers.add_parser("fit", help="fit vane offset and speed calibration")
    fit_parser.add_argument("log", help="CSV written by 'log'")
    fit_parser.add_argument("--min-len", type=int, default=30,
                            help="min seconds of steady running (default 30)")
    fit_parser.add_argument("--min-sog", type=float, default=3.0,
                            help="ignore samples below this SOG in kn (default 3)")
    fit_parser.add_argument("--sog-tol", type=float, default=0.8,
                            help="max SOG spread within a plateau, kn (default 0.8)")
    fit_parser.add_argument("--head-tol", type=float, default=5.0,
                            help="max heading spread within a plateau, deg (default 5)")
    fit_parser.add_argument("--speed-tol", type=float, default=2.0,
                            help="max SOG difference between paired runs, kn (default 2)")
    fit_parser.add_argument("--recip-tol", type=float, default=20.0,
                            help="max departure from exactly 180 deg apart (default 20)")
    fit_parser.set_defaults(func=cmd_fit)

    args = parser.parse_args()
    if not getattr(args, "func", None):
        parser.print_help()
        return 1
    return args.func(args) or 0


if __name__ == "__main__":
    sys.exit(main())
