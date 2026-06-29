#!/usr/bin/env python3
"""Scenario 2 (plan §4 / research §9): a short 3-waypoint route the emulator
navigates **to completion**, exercising the controller's Follow-Garmin path:
route-in (WPL/RTE), RMB-follow, leg auto-advance, and disengage-at-end (RMB -> 'V').

Legs are deliberately short and the speed exaggerated so the route finishes in
~30-40 s rather than the many minutes a realistic 6 kn boat would take.

How to run (the assertions are observed on the Navigator, since the Mac is not on
the SoberPilot network):

  1. Navigator:  telnet 10.20.1.1 23   ->  f1     (arm Follow-Garmin)
  2. Navigator:  start the UDP 8888 sniffer (see emulator/README or plan §1a test C)
  3. Mac:        python scenarios/scenario2_route.py --port /dev/cu.usbserial-XXXX

Expected on the Navigator's ~APDAT stream:
  - waypoint_set -> 1 and wp_lat/wp_lon tracking each RMB destination (SC2B, SC2C);
  - nav_enabled -> 1 once armed + first RMB 'A' (mode -> 2 only if the controller
    has its own GPS fix; without a fix it shows mode 1 / compass-fallback, nav on);
  - at route end (RMB 'V'), nav_enabled -> 0  (disengage-at-end).
  Disarmed (skip step 1, or `f0`): waypoint still tracks the RMB dest, but
  nav_enabled stays 0 until the operator presses Enable.
"""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import garmin_emulator as ge  # noqa: E402

# ~0.3 nm legs (0.005 deg) so the route completes quickly under exaggerated speed.
SCENARIO2_ROUTE = [
    ("SC2A", 37.8000, -122.4000),
    ("SC2B", 37.8050, -122.4000),
    ("SC2C", 37.8100, -122.4050),
]


def main() -> int:
    p = argparse.ArgumentParser(description="Scenario 2: navigate a short route to completion")
    p.add_argument("--port", required=True, help="FTDI serial device")
    p.add_argument("--baud", type=int, default=4800)
    p.add_argument("--speed", type=float, default=100.0, help="knots (exaggerated for speed)")
    p.add_argument("--interval", type=float, default=0.5)
    p.add_argument("--navigate-after", type=float, default=4.0)
    args = p.parse_args()

    emu = ge.build_serial_emulator(
        args.port, args.baud, route=SCENARIO2_ROUTE, route_id="SCEN02",
        speed=args.speed, log=lambda m: print("# " + m, file=sys.stderr))
    print("# scenario 2: idle, then navigate SCEN02 to completion", file=sys.stderr)
    ge.run(emu, interval_s=args.interval, navigate_after_s=args.navigate_after,
           duration_s=None)
    print("# scenario 2 done — route should have ended with RMB 'V' (disengage)",
          file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
