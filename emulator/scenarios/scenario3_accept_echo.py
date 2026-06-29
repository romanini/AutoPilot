"""Scenario 3: Accept-and-echo — verify a plugin-pushed route arrives at the Garmin UART.

Operator sequence:
  1. Load a route in OpenCPN and activate it (right-click → Activate Route).
  2. Open the AutoPilot panel on the Navigator and press "Send Rte".
  3. Run this script on the Mac with the FTDI tap wired up.

The plugin wraps each WPL+RTE sentence as ~APTX and sends it to the controller
(UDP 8889). The controller writes them verbatim to the Garmin UART (Serial1,
4800-8N1, A0/A1). This script reads them back via the FTDI tap (5 V side at the
JST-8 Garmin-In connector — see plan §4 for wiring), validates the route, then
optionally replays it as if the Garmin were navigating each leg (emitting
RMB+XTE+BOD per tick).  That replay feeds back into the controller as ~APRX
broadcasts, which the plugin receives — closing the round-trip loop that step 5
(de-dup) depends on.

Exit codes: 0 = route received and valid, 1 = timeout or parse error.
"""
from __future__ import annotations

import argparse
import os
import sys
import time
from datetime import datetime, timezone
from typing import Callable, Dict, List, Optional, Tuple

# Ensure emulator/ is on sys.path so nmea/geo/garmin_emulator are importable
# whether this script is run from the repo root or from emulator/.
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import geo  # noqa: E402
import nmea  # noqa: E402
from garmin_emulator import GarminEmulator  # noqa: E402

# How long to wait for the *first* sentence from the controller.
WAIT_TIMEOUT_S = 60.0
# Max gap between consecutive WPL/RTE sentences once the route starts arriving.
ROUTE_TIMEOUT_S = 10.0
# Rate at which idle RMC+GGA are sent to prove "Garmin is present".
IDLE_INTERVAL_S = 1.0
# Tick rate during the navigation replay.
NAV_INTERVAL_S = 1.0

# Idle position parked at the first waypoint of the demo route.
IDLE_LAT = 37.808
IDLE_LON = -122.470


# ---------------------------------------------------------------------------
# Serial port helpers
# ---------------------------------------------------------------------------

def _open_port(port: str, baud: int):
    """Return (writer_fn, reader_fn) backed by a pyserial port."""
    import serial  # lazy so --dry-run needs no pyserial

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
        line = buf[:nl].decode("ascii", "replace").strip()
        del buf[:nl + 1]
        return line or None

    return writer, reader


# ---------------------------------------------------------------------------
# Route collection
# ---------------------------------------------------------------------------

def collect_route(
    writer: Callable[[str], None],
    reader: Callable[[], Optional[str]],
    log: Callable[[str], None],
) -> Tuple[Dict[str, dict], List[str], str]:
    """Emit idle RMC+GGA and collect WPL+RTE sentences until a complete route arrives.

    Returns ``(wpl_by_name, rte_waypoint_order, route_id)``.
    Raises ``TimeoutError`` if no route arrives within the configured timeouts.
    """
    wpl: Dict[str, dict] = {}   # name → {name, lat, lon}
    rte_order: List[str] = []
    rte_id = ""
    rte_total = 0
    rte_count = 0

    start = time.monotonic()
    last_emit = 0.0
    first_rx: Optional[float] = None  # time we first saw a WPL or RTE

    while True:
        now = time.monotonic()

        # Timeout: nothing seen yet within WAIT_TIMEOUT_S, or gap after first.
        if first_rx is None and now - start > WAIT_TIMEOUT_S:
            raise TimeoutError(
                f"No WPL/RTE received within {WAIT_TIMEOUT_S:.0f} s — "
                "check that 'Send Rte' was pressed and the FTDI tap is wired."
            )
        if first_rx is not None and now - first_rx > ROUTE_TIMEOUT_S:
            raise TimeoutError(
                f"Route assembly stalled after {ROUTE_TIMEOUT_S:.0f} s "
                f"({rte_count}/{rte_total} RTE messages received)."
            )

        # Emit idle position so the controller knows Garmin is present.
        if now - last_emit >= IDLE_INTERVAL_S:
            when = datetime.now(timezone.utc)
            writer(nmea.rmc(when, IDLE_LAT, IDLE_LON, 0.0, 0.0))
            writer(nmea.gga(when, IDLE_LAT, IDLE_LON))
            last_emit = now

        line = reader()
        if not line:
            time.sleep(0.02)
            continue

        p = nmea.parse(line)
        if not p["valid"]:
            log(f"bad checksum (ignored): {line!r}")
            continue

        stype = p["type"]
        if stype == "WPL":
            first_rx = now
            entry = {"name": p["name"], "lat": p["lat"], "lon": p["lon"]}
            wpl[p["name"]] = entry
            log(f"WPL: {p['name']}  {p['lat']:.5f}, {p['lon']:.5f}")

        elif stype == "RTE":
            first_rx = now
            if p["num"] == 1:
                # First (or only) RTE message: start a fresh assembly.
                rte_order = []
                rte_id    = p["route_id"] or ""
                rte_total = p["total"] or 1
                rte_count = 0
            rte_order.extend(p["waypoints"])
            rte_count += 1
            log(f"RTE {p['num']}/{p['total']} id={rte_id}: {p['waypoints']}")
            if rte_count >= rte_total:
                return wpl, rte_order, rte_id

        time.sleep(0.02)


# ---------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------

def validate_route(
    wpl: Dict[str, dict],
    order: List[str],
    route_id: str,
    log: Callable[[str], None],
) -> bool:
    """Return True if every waypoint in the RTE order has a matching WPL entry."""
    missing = [name for name in order if name not in wpl]
    if missing:
        for name in missing:
            log(f"ERROR: RTE references '{name}' but no WPL received for it")
        return False

    log(
        f"Route '{route_id}' valid: {len(order)} waypoints, "
        "all WPL entries present"
    )
    for name in order:
        e = wpl[name]
        log(f"  {name}  {e['lat']:.5f}, {e['lon']:.5f}")
    return True


# ---------------------------------------------------------------------------
# Replay (the "echo" half of accept-and-echo)
# ---------------------------------------------------------------------------

def replay_route(
    wpl: Dict[str, dict],
    order: List[str],
    route_id: str,
    writer: Callable[[str], None],
    reader: Callable[[], Optional[str]],
    log: Callable[[str], None],
) -> None:
    """Emit RMB+XTE+BOD for each leg, dead-reckoning toward each waypoint.

    Does NOT re-emit WPL+RTE (the plugin already pushed the route; re-sending
    would create a duplicate on the Garmin).  Sets emu.navigating = True
    directly, bypassing GarminEmulator.start_navigation().

    The RMB/XTE/BOD sentences are relayed by the controller as ~APRX broadcasts.
    The plugin receives them and (in step 5) should recognize this as its own
    route and not create a duplicate in OpenCPN.
    """
    route = [(name, wpl[name]["lat"], wpl[name]["lon"]) for name in order]
    emu = GarminEmulator(
        writer=writer,
        reader=reader,
        route=route,
        route_id=route_id,
        speed_kn=6.0,
        log=lambda m: log(f"[emu] {m}"),
    )
    # Skip start_navigation() which would re-emit WPL+RTE; engage directly.
    emu.navigating = True
    emu.leg = 0
    emu.lat, emu.lon = route[0][1], route[0][2]
    log(f"replaying route '{route_id}' ({len(route)} waypoints) at 6 kn")

    last = time.monotonic()
    try:
        while emu.navigating:
            now = time.monotonic()
            dt = now - last
            last = now
            emu.emit_nav(datetime.now(timezone.utc), dt)
            emu.drain_input()
            time.sleep(NAV_INTERVAL_S)
    except KeyboardInterrupt:
        log("replay interrupted by user")
        return

    log("replay complete — RMB status V sent, navigation ended")


# ---------------------------------------------------------------------------
# Dry-run (no hardware needed)
# ---------------------------------------------------------------------------

def _dry_run(log: Callable[[str], None]) -> int:
    """Validate with a canned 3-waypoint route; no serial port required."""
    log("dry-run mode: simulating received route (no serial port)")
    wpl = {
        "WPT01": {"name": "WPT01", "lat": 37.808, "lon": -122.470},
        "WPT02": {"name": "WPT02", "lat": 37.700, "lon": -122.520},
        "WPT03": {"name": "WPT03", "lat": 37.600, "lon": -122.600},
    }
    order = ["WPT01", "WPT02", "WPT03"]
    route_id = "OCPN01"
    ok = validate_route(wpl, order, route_id, log)
    if ok:
        # Smoke-test the replay helper logic without emitting to serial.
        route = [(n, wpl[n]["lat"], wpl[n]["lon"]) for n in order]
        emu = GarminEmulator(writer=lambda _: None, reader=lambda: None,
                             route=route, route_id=route_id)
        emu.navigating = True
        emu.leg = 0
        emu.lat, emu.lon = route[0][1], route[0][2]
        when = datetime.now(timezone.utc)
        emu.emit_nav(when, dt_s=1.0)
        log("dry-run replay tick OK")
    return 0 if ok else 1


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def run_scenario(port: Optional[str], baud: int, replay: bool,
                 dry_run: bool) -> int:
    log = lambda m: print(f"[scen3] {m}", flush=True)

    if dry_run:
        return _dry_run(log)

    writer, reader = _open_port(port, baud)
    log(f"opened {port} @ {baud} baud")
    log("waiting for route from controller (press 'Send Rte' in the plugin)…")

    try:
        wpl, order, route_id = collect_route(writer, reader, log)
    except TimeoutError as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1

    if not validate_route(wpl, order, route_id, log):
        return 1

    if replay:
        replay_route(wpl, order, route_id, writer, reader, log)

    return 0


def main(argv=None) -> int:
    p = argparse.ArgumentParser(
        description="Scenario 3: accept a plugin-pushed route from the Garmin UART "
                    "and replay it as if navigating."
    )
    p.add_argument(
        "--port",
        help="serial device for the FTDI tap (e.g. /dev/cu.usbserial-XXXX). "
             "Omit with --dry-run.",
    )
    p.add_argument("--baud", type=int, default=4800)
    p.add_argument(
        "--replay", action="store_true", default=True,
        help="after receiving the route, emit RMB as if navigating (default on)",
    )
    p.add_argument("--no-replay", dest="replay", action="store_false",
                   help="just receive and validate, do not replay")
    p.add_argument(
        "--dry-run", action="store_true",
        help="run with a canned route, no serial port needed (smoke test)",
    )
    args = p.parse_args(argv)
    if not args.dry_run and not args.port:
        p.error("--port is required unless --dry-run is given")
    return run_scenario(args.port, args.baud, args.replay, args.dry_run)


if __name__ == "__main__":
    raise SystemExit(main())
