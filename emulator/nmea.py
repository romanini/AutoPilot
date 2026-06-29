"""NMEA-0183 build + checksum + parse — the authoritative Python side of the
wire contract (`.claude/docs/WireContract.md` §6).

This module is the Python counterpart to the controller's C implementation in
`Arduino/controller/garmin.ino` (`nmea_checksum_ok`, `garmin_should_relay`) and
to the OpenCPN plugin's serialize/parse. All three must agree byte-for-byte on
the same sentences; the pytest suite pins this module to the exact checksums the
controller computes, so a drift on either side fails loudly.

Sentence builders return a single line WITHOUT trailing CRLF (matching how the
contract frames `~APTX`/`~APRX` payloads — the transport, i.e. the serial writer,
appends `\\r\\n`). Coordinates use the Garmin `ddmm.mmm` / `dddmm.mmm` form, whose
~0.001' rounding (~1.85 m) is why the contract specifies a position epsilon for
round-trip de-dup.
"""

from __future__ import annotations

from datetime import datetime
from typing import Optional

# The five sentence types the controller relays as ~APRX (contract §4). The
# high-rate RMC/GGA/GLL/VTG are intentionally NOT in this set.
RELAY_TYPES = frozenset({"WPL", "RTE", "RMB", "XTE", "BOD"})

DEFAULT_TALKER = "GP"


# ---------------------------------------------------------------------------
# Checksum + framing
# ---------------------------------------------------------------------------

def checksum(body: str) -> str:
    """XOR of every char between '$' and '*', as two uppercase hex digits.

    `body` is the sentence content *without* the leading '$' or trailing '*HH'
    (e.g. "GPWPL,3723.460,N,12158.940,W,WPT01"). Mirrors the controller's
    `nmea_checksum_ok` exactly.
    """
    cs = 0
    for ch in body:
        cs ^= ord(ch)
    return f"{cs:02X}"


def frame(body: str) -> str:
    """Wrap a body as a complete sentence: '$' + body + '*HH'."""
    return f"${body}*{checksum(body)}"


def verify(line: str) -> bool:
    """True if `line` is a well-formed sentence with a matching checksum."""
    line = line.strip()
    if not line.startswith("$") or "*" not in line:
        return False
    star = line.rindex("*")
    body = line[1:star]
    given = line[star + 1:star + 3]
    if len(given) < 2:
        return False
    try:
        return int(given, 16) == int(checksum(body), 16)
    except ValueError:
        return False


def split(line: str):
    """Return (talker, type, [fields]) for a sentence line.

    Strips '$' and the '*HH' suffix. For "$GPWPL,..." -> ("GP", "WPL", [...]).
    Matches the controller's "type at offset 3, talker-agnostic" relay rule.
    """
    line = line.strip()
    if line.startswith("$"):
        line = line[1:]
    if "*" in line:
        line = line[:line.rindex("*")]
    parts = line.split(",")
    addr = parts[0]
    talker = addr[:2] if len(addr) >= 5 else ""
    stype = addr[2:5] if len(addr) >= 5 else addr
    return talker, stype, parts[1:]


def sentence_type(line: str) -> str:
    return split(line)[1]


def is_relay_type(line: str) -> bool:
    """Type-only relay test (mirrors `garmin_should_relay`; no checksum gate)."""
    return sentence_type(line) in RELAY_TYPES


# ---------------------------------------------------------------------------
# Coordinates (ddmm.mmm / dddmm.mmm with hemisphere)
# ---------------------------------------------------------------------------

def format_lat(deg: float):
    hemi = "N" if deg >= 0 else "S"
    deg = abs(deg)
    d = int(deg)
    minutes = (deg - d) * 60.0
    return f"{d:02d}{minutes:06.3f}", hemi


def format_lon(deg: float):
    hemi = "E" if deg >= 0 else "W"
    deg = abs(deg)
    d = int(deg)
    minutes = (deg - d) * 60.0
    return f"{d:03d}{minutes:06.3f}", hemi


def parse_coord(value: str, hemi: str) -> Optional[float]:
    """Parse a ddmm.mmm/dddmm.mmm value + hemisphere into signed degrees.

    The whole minutes are always the two digits immediately left of the decimal
    point, so the same logic serves both latitude and longitude.
    """
    if not value:
        return None
    point = value.index(".") if "." in value else len(value)
    deg_digits = value[:point - 2]
    minutes = float(value[point - 2:])
    deg = int(deg_digits) if deg_digits else 0
    result = deg + minutes / 60.0
    if hemi in ("S", "W"):
        result = -result
    return result


# ---------------------------------------------------------------------------
# Sentence builders
# ---------------------------------------------------------------------------

def rmc(when: datetime, lat: float, lon: float, sog_kn: float, cog_deg: float,
        status: str = "A", talker: str = DEFAULT_TALKER) -> str:
    la, ns = format_lat(lat)
    lo, ew = format_lon(lon)
    body = (f"{talker}RMC,{when:%H%M%S},{status},{la},{ns},{lo},{ew},"
            f"{sog_kn:.1f},{cog_deg:.1f},{when:%d%m%y},,")
    return frame(body)


def gga(when: datetime, lat: float, lon: float, fix_quality: int = 1,
        num_sat: int = 8, hdop: float = 0.9, alt_m: float = 0.0,
        talker: str = DEFAULT_TALKER) -> str:
    la, ns = format_lat(lat)
    lo, ew = format_lon(lon)
    body = (f"{talker}GGA,{when:%H%M%S},{la},{ns},{lo},{ew},{fix_quality},"
            f"{num_sat:02d},{hdop:.1f},{alt_m:.1f},M,0.0,M,,")
    return frame(body)


def wpl(lat: float, lon: float, name: str, talker: str = DEFAULT_TALKER) -> str:
    la, ns = format_lat(lat)
    lo, ew = format_lon(lon)
    return frame(f"{talker}WPL,{la},{ns},{lo},{ew},{name}")


def rte(waypoint_ids, route_id: str, complete: bool = True,
        talker: str = DEFAULT_TALKER, max_sentence: int = 80):
    """Serialize a route's waypoint IDs into one or more RTE sentences.

    Greedily packs IDs so each framed sentence stays within `max_sentence` chars
    (NMEA's limit is 82 incl. CRLF; we target 80 for the bare line). Returns a
    list of sentence strings with correct total/sequence numbers.
    """
    cw = "c" if complete else "w"
    chunks = []
    cur: list = []
    for wid in waypoint_ids:
        trial = cur + [wid]
        # Size with a single-digit total/num placeholder; routes needing >9
        # sentences are not expected for the 276c.
        body = f"{talker}RTE,9,9,{cw},{route_id}," + ",".join(trial)
        if len(body) + 4 > max_sentence and cur:   # +4 = '$' + '*HH'
            chunks.append(cur)
            cur = [wid]
        else:
            cur = trial
    if cur:
        chunks.append(cur)
    total = len(chunks)
    return [frame(f"{talker}RTE,{total},{i},{cw},{route_id}," + ",".join(c))
            for i, c in enumerate(chunks, 1)]


def rmb(status: str, xte_nm: float, steer: str, origin_id: str, dest_id: str,
        dest_lat: float, dest_lon: float, range_nm: float, bearing: float,
        velocity: float, arrival: str, talker: str = DEFAULT_TALKER) -> str:
    la, ns = format_lat(dest_lat)
    lo, ew = format_lon(dest_lon)
    body = (f"{talker}RMB,{status},{xte_nm:.2f},{steer},{origin_id},{dest_id},"
            f"{la},{ns},{lo},{ew},{range_nm:.1f},{bearing:.1f},{velocity:.1f},"
            f"{arrival}")
    return frame(body)


def xte(magnitude_nm: float, steer: str, talker: str = DEFAULT_TALKER) -> str:
    return frame(f"{talker}XTE,A,A,{magnitude_nm:.2f},{steer},N")


def bod(bearing_true: float, dest_id: str, origin_id: str,
        bearing_mag: Optional[float] = None, talker: str = DEFAULT_TALKER) -> str:
    bm = f"{bearing_mag:.1f}" if bearing_mag is not None else ""
    return frame(f"{talker}BOD,{bearing_true:.1f},T,{bm},M,{dest_id},{origin_id}")


def serialize_route(waypoints, route_id: str, complete: bool = True,
                    talker: str = DEFAULT_TALKER):
    """A route -> WPL burst (one per waypoint) followed by the RTE sentence(s).

    `waypoints` is a list of (name, lat, lon). This is the Python parity of the
    plugin's C++ route serializer (plan §3.1).
    """
    lines = [wpl(lat, lon, name, talker) for (name, lat, lon) in waypoints]
    lines += rte([name for (name, _, _) in waypoints], route_id, complete, talker)
    return lines


# ---------------------------------------------------------------------------
# Parsing
# ---------------------------------------------------------------------------

def _num(fields, i, conv=float):
    try:
        v = fields[i]
        return conv(v) if v != "" else None
    except (IndexError, ValueError):
        return None


def _str(fields, i):
    try:
        return fields[i]
    except IndexError:
        return None


def parse(line: str) -> dict:
    """Parse a sentence into a dict: always has 'talker','type','fields','valid';
    recognised types add their decoded fields.
    """
    talker, stype, f = split(line)
    out = {"talker": talker, "type": stype, "fields": f, "valid": verify(line)}

    if stype == "RMB":
        out.update(
            status=_str(f, 0), xte_nm=_num(f, 1), steer=_str(f, 2),
            origin_id=_str(f, 3), dest_id=_str(f, 4),
            dest_lat=parse_coord(_str(f, 5) or "", _str(f, 6) or ""),
            dest_lon=parse_coord(_str(f, 7) or "", _str(f, 8) or ""),
            range_nm=_num(f, 9), bearing=_num(f, 10), velocity=_num(f, 11),
            arrival=_str(f, 12),
        )
    elif stype == "WPL":
        out.update(
            lat=parse_coord(_str(f, 0) or "", _str(f, 1) or ""),
            lon=parse_coord(_str(f, 2) or "", _str(f, 3) or ""),
            name=_str(f, 4),
        )
    elif stype == "RTE":
        out.update(
            total=_num(f, 0, int), num=_num(f, 1, int), mode=_str(f, 2),
            route_id=_str(f, 3), waypoints=[w for w in f[4:] if w != ""],
        )
    elif stype == "XTE":
        out.update(magnitude_nm=_num(f, 2), steer=_str(f, 3), units=_str(f, 4))
    elif stype == "BOD":
        out.update(
            bearing_true=_num(f, 0), bearing_mag=_num(f, 2),
            dest_id=_str(f, 4), origin_id=_str(f, 5),
        )
    elif stype == "RMC":
        out.update(
            time=_str(f, 0), status=_str(f, 1),
            lat=parse_coord(_str(f, 2) or "", _str(f, 3) or ""),
            lon=parse_coord(_str(f, 4) or "", _str(f, 5) or ""),
            sog_kn=_num(f, 6), cog_deg=_num(f, 7), date=_str(f, 8),
        )
    elif stype == "GGA":
        out.update(
            time=_str(f, 0),
            lat=parse_coord(_str(f, 1) or "", _str(f, 2) or ""),
            lon=parse_coord(_str(f, 3) or "", _str(f, 4) or ""),
            fix_quality=_num(f, 5, int), num_sat=_num(f, 6, int),
        )
    return out
