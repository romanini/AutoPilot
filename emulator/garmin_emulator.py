"""Garmin GPSMAP 276c emulator — a serial NMEA device that stands in for the
real chartplotter on the controller's Garmin UART (plan §4.2, Tier 2).

It does three things:
  1. streams idle position (RMC + GGA) at a fixed location;
  2. on command, navigates a route: emits the WPL burst + RTE once, then a cyclic
     RMB (+ XTE + BOD) for the active leg, dead-reckoning the boat along each leg,
     advancing with the arrival flag, and ending the route with RMB status 'V';
  3. reads back whatever the controller writes to the UART (the route the OpenCPN
     plugin pushed via ~APTX) and parses it — the round-trip verifier for
     OpenCPN->Garmin push and the §6 de-dup test.

The state machine (`GarminEmulator`) is transport-agnostic: it takes a `writer`
callable (one NMEA line out) and an optional `reader` callable (one line in, or
None). `run_serial()` wires those to a pyserial port at 4800-8N1; `--dry-run`
wires the writer to stdout so the whole flow can be exercised with no hardware.
"""

from __future__ import annotations

import argparse
import sys
import time
from dataclasses import dataclass, field
from datetime import datetime, timezone
from typing import Callable, List, Optional, Tuple

import geo
import nmea

# A demo route off the San Francisco coast: (name, lat, lon).
DEMO_ROUTE: List[Tuple[str, float, float]] = [
    ("WPT01", 37.808, -122.470),
    ("WPT02", 37.700, -122.520),
    ("WPT03", 37.600, -122.600),
]
DEMO_ROUTE_ID = "GARMN1"

ARRIVAL_RADIUS_NM = 0.05   # ~93 m; advance to the next leg inside this


@dataclass
class GarminEmulator:
    writer: Callable[[str], None]
    reader: Optional[Callable[[], Optional[str]]] = None
    route: List[Tuple[str, float, float]] = field(default_factory=lambda: list(DEMO_ROUTE))
    route_id: str = DEMO_ROUTE_ID
    speed_kn: float = 6.0
    log: Callable[[str], None] = lambda msg: None

    # runtime state
    lat: float = field(init=False)
    lon: float = field(init=False)
    navigating: bool = field(default=False, init=False)
    leg: int = field(default=0, init=False)        # origin = route[leg], dest = route[leg+1]
    received: List[dict] = field(default_factory=list, init=False)

    def __post_init__(self):
        # Start parked at the first waypoint.
        _, self.lat, self.lon = self.route[0]

    # -- emission -----------------------------------------------------------

    def emit_idle(self, when: datetime) -> None:
        """One idle tick: RMC + GGA (both dropped by the controller's relay)."""
        self.writer(nmea.rmc(when, self.lat, self.lon, 0.0, 0.0, status="A"))
        self.writer(nmea.gga(when, self.lat, self.lon))

    def start_navigation(self) -> None:
        """Begin navigating: park at WP1 and push the route once (WPL + RTE)."""
        _, self.lat, self.lon = self.route[0]
        self.leg = 0
        self.navigating = True
        for line in nmea.serialize_route(self.route, self.route_id):
            self.writer(line)
        self.log(f"navigation started: route {self.route_id}, {len(self.route)} wpts")

    def emit_nav(self, when: datetime, dt_s: float) -> None:
        """One navigation tick: advance the boat and emit RMB/XTE/BOD (+RMC/GGA).

        Always emits RMC/GGA too, so the relay filter is exercised (those must be
        dropped while WPL/RTE/RMB/XTE/BOD are relayed).
        """
        self.writer(nmea.rmc(when, self.lat, self.lon, self.speed_kn,
                             geo.bearing(*self._leg_pts()[0], *self._dest()[1:]),
                             status="A"))
        self.writer(nmea.gga(when, self.lat, self.lon))

        if not self.navigating:
            return

        (o_lat, o_lon), (d_name, d_lat, d_lon), o_name = self._leg_geom()
        rng = geo.distance_nm(self.lat, self.lon, d_lat, d_lon)
        brg = geo.bearing(self.lat, self.lon, d_lat, d_lon)
        xte_nm, steer = geo.cross_track_nm(self.lat, self.lon, o_lat, o_lon, d_lat, d_lon)
        leg_course = geo.bearing(o_lat, o_lon, d_lat, d_lon)
        arrived = rng <= ARRIVAL_RADIUS_NM

        self.writer(nmea.rmb("A", xte_nm, steer, o_name, d_name, d_lat, d_lon,
                             rng, brg, self.speed_kn, "A" if arrived else "V"))
        self.writer(nmea.xte(xte_nm, steer))
        self.writer(nmea.bod(leg_course, d_name, o_name))

        # Dead-reckon toward the destination for this tick.
        step_nm = self.speed_kn * (dt_s / 3600.0)
        if step_nm >= rng or arrived:
            self.lat, self.lon = d_lat, d_lon
            self._advance_leg()
        else:
            self.lat, self.lon = geo.destination_point(self.lat, self.lon, brg, step_nm)

    def _advance_leg(self) -> None:
        self.leg += 1
        if self.leg + 1 >= len(self.route):
            # Reached the final waypoint: end navigation, signal "no destination".
            self.navigating = False
            last = self.route[-1]
            self.writer(nmea.rmb("V", 0.0, "L", last[0], last[0],
                                 last[1], last[2], 0.0, 0.0, 0.0, "A"))
            self.log("navigation complete: route end, RMB status V")

    # -- read-back ----------------------------------------------------------

    def drain_input(self) -> List[dict]:
        """Read and parse any lines the controller wrote back (pushed route)."""
        got = []
        if self.reader is None:
            return got
        while True:
            line = self.reader()
            if not line:
                break
            line = line.strip()
            if not line:
                continue
            parsed = nmea.parse(line)
            self.received.append(parsed)
            got.append(parsed)
            tag = "ok " if parsed["valid"] else "BAD"
            self.log(f"<- readback [{tag}] {parsed['type']}: {line}")
        return got

    # -- geometry helpers ---------------------------------------------------

    def _dest(self):
        d = self.route[min(self.leg + 1, len(self.route) - 1)]
        return d  # (name, lat, lon)

    def _leg_pts(self):
        o = self.route[self.leg]
        return (o[1], o[2]), self._dest()

    def _leg_geom(self):
        o = self.route[self.leg]
        d = self._dest()
        return (o[1], o[2]), d, o[0]


# ---------------------------------------------------------------------------
# Runners
# ---------------------------------------------------------------------------

def run(emu: GarminEmulator, *, interval_s: float, navigate_after_s: float,
        duration_s: Optional[float]) -> None:
    """Drive the emulator on a fixed tick until the route ends (or duration)."""
    start = time.monotonic()
    last = start
    started_nav = False
    while True:
        now = time.monotonic()
        dt = now - last
        last = now
        when = datetime.now(timezone.utc)

        if (not started_nav) and (now - start) >= navigate_after_s:
            emu.start_navigation()
            started_nav = True

        if emu.navigating:
            emu.emit_nav(when, dt)
        else:
            emu.emit_idle(when)

        emu.drain_input()

        # Stop conditions: explicit duration, or a little after the route ends.
        elapsed = now - start
        if duration_s is not None and elapsed >= duration_s:
            return
        if started_nav and not emu.navigating and elapsed >= navigate_after_s + 1.0:
            # one extra idle tick already sent; let read-back settle then exit
            emu.drain_input()
            return
        time.sleep(interval_s)


def _stdout_writer(line: str) -> None:
    sys.stdout.write(line + "\r\n")
    sys.stdout.flush()


def build_serial_emulator(port: str, baud: int = 4800, route=None,
                          route_id: str = DEMO_ROUTE_ID, speed: float = 6.0,
                          log: Optional[Callable[[str], None]] = None) -> "GarminEmulator":
    """Construct a GarminEmulator wired to a real serial port (4800-8N1).

    Shared by the CLI and the scenario drivers so the pyserial plumbing lives in
    one place. `pyserial` is imported lazily so --dry-run / unit tests don't need it.
    """
    import serial
    ser = serial.Serial(port, baud, timeout=0)
    buf = bytearray()

    def writer(line: str) -> None:
        ser.write((line + "\r\n").encode("ascii", "replace"))

    def reader() -> Optional[str]:
        nonlocal buf
        data = ser.read(256)
        if data:
            buf.extend(data)
        nl = buf.find(b"\n")
        if nl < 0:
            return None
        line = buf[:nl].decode("ascii", "replace")
        del buf[:nl + 1]
        return line

    kwargs = {"writer": writer, "reader": reader, "route_id": route_id,
              "speed_kn": speed, "log": log or (lambda m: None)}
    if route is not None:
        kwargs["route"] = list(route)
    return GarminEmulator(**kwargs)


def run_serial(port: str, baud: int, **kw) -> None:
    emu = build_serial_emulator(port, baud, speed=kw["speed"],
                                log=lambda m: print(m, file=sys.stderr))
    run(emu, interval_s=kw["interval"], navigate_after_s=kw["navigate_after"],
        duration_s=kw["duration"])


def main(argv=None) -> int:
    p = argparse.ArgumentParser(description="Garmin 276c serial emulator")
    p.add_argument("--port", help="serial device (e.g. /dev/cu.usbserial-XXXX). "
                                   "Omit with --dry-run.")
    p.add_argument("--baud", type=int, default=4800)
    p.add_argument("--speed", type=float, default=6.0, help="boat speed (knots)")
    p.add_argument("--interval", type=float, default=1.0, help="tick seconds")
    p.add_argument("--navigate-after", type=float, default=3.0,
                   help="seconds of idle before starting navigation")
    p.add_argument("--duration", type=float, default=None,
                   help="stop after N seconds (default: a bit past route end)")
    p.add_argument("--dry-run", action="store_true",
                   help="print sentences to stdout instead of opening a port")
    args = p.parse_args(argv)

    if args.dry_run:
        emu = GarminEmulator(writer=_stdout_writer, reader=None, speed_kn=args.speed,
                             log=lambda m: print("# " + m, file=sys.stderr))
        run(emu, interval_s=args.interval, navigate_after_s=args.navigate_after,
            duration_s=args.duration)
        return 0

    if not args.port:
        p.error("--port is required unless --dry-run is given")
    run_serial(args.port, args.baud, speed=args.speed, interval=args.interval,
               navigate_after=args.navigate_after, duration=args.duration)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
