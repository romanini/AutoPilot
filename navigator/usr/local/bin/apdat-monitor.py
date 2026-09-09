#!/usr/bin/env python3
# Listens for the controller's ~APDAT broadcast (UDP 8888, see
# firmware/Arduino/controller/publish.ino) and redraws a live snapshot of it in
# place, one screen that keeps updating - not a scrolling log. The fields shown
# mirror the controller's telnet `p` command (controller/telnet.ino), except
# for the motor-enable millivolt reading, which isn't on the wire in ~APDAT.
#
# Usage: apdat-monitor.py [--port 8888] [--bind ADDR]
# Run from any machine associated with the SoberPilot Wi-Fi (10.20.1.x) - the
# navigator's wlan0 included.

import argparse
import socket
import sys
import time

# Order matches the controller's snprintf() field order exactly (see
# publish.ino) - fields are only ever appended, never reordered, so trailing
# names here are the newest additions.
FIELDS = [
    "year", "month", "day", "hour", "minute",
    "fix", "fixquality", "satellites",
    "nav_enabled", "mode", "waypoint_set", "wp_lat", "wp_lon",
    "heading_desired", "heading", "pitch", "roll", "stability",
    "bearing", "bearing_correction",
    "speed", "distance", "course",
    "location_lat", "location_lon",
    "nav_source",
    "autotune_state",
    "cog_damped", "cog_damped_valid",
    "rudder_angle", "rudder_ok",
    "awa", "aws_kn", "wind_ok",
    "twa", "tws_kn", "true_wind_ok",
    "air_temp_c", "temp_ok",
]

# Fields up to and including location_lon are the original frame; everything
# past that has been appended over time. A frame this short still has enough
# to draw the core dashboard, so it's the minimum rather than requiring all 39.
MIN_FIELDS = 25

NAV_SOURCES = {0: "NONE", 1: "GARMIN", 2: "OPENCPN"}
FIX_QUALITY = {0: "n/a", 1: "GPS", 2: "DGPS"}


def parse_apdat(line):
    line = line.strip()
    if not line.startswith("~APDAT,") or not line.endswith("$"):
        return None
    parts = line[len("~APDAT,"):-1].split(",")
    if len(parts) < MIN_FIELDS:
        return None
    return dict(zip(FIELDS, parts))


def as_int(values, name, default=0):
    try:
        return int(float(values[name]))
    except (KeyError, ValueError):
        return default


def as_float(values, name, default=0.0):
    try:
        return float(values[name])
    except (KeyError, ValueError):
        return default


def render(values, addr, packet_count, received_at):
    lines = []
    stamp = time.strftime("%H:%M:%S", time.localtime(received_at))
    lines.append(f"~APDAT monitor - from {addr[0]}:{addr[1]}, packet #{packet_count} at {stamp}")
    lines.append("=" * 70)

    date_line = "Date&Time: {}/{}/{:02d} {}:{:02d}".format(
        as_int(values, "month"), as_int(values, "day"), as_int(values, "year"),
        as_int(values, "hour"), as_int(values, "minute"),
    )
    if as_int(values, "fix"):
        quality = FIX_QUALITY.get(as_int(values, "fixquality"), "?")
        date_line += f"  {quality} ({as_int(values, 'satellites')})"
    lines.append(date_line)

    lines.append(f"Nav source: {NAV_SOURCES.get(as_int(values, 'nav_source'), '?')}")
    lines.append(f"Navigation: {'enabled' if as_int(values, 'nav_enabled') else 'disabled'}")

    mode = as_int(values, "mode")
    if mode == 2:
        lines.append(f"Destination: waypoint {as_float(values, 'wp_lat'):.6f},{as_float(values, 'wp_lon'):.6f}")
    elif mode == 1:
        lines.append(f"Destination: compass {as_float(values, 'heading_desired'):.1f}")
    else:
        lines.append("Destination: disabled")

    heading_line = f"Heading: {as_float(values, 'heading'):.2f} Bearing: "
    if mode > 0:
        correction = as_float(values, "bearing_correction")
        heading_line += f"{as_float(values, 'bearing'):.1f} {abs(correction):.1f} {'R' if correction > 0 else 'L'}"
    else:
        heading_line += "N/A"
    lines.append(heading_line)

    lines.append(
        "Speed: {:.2f} Distance: {:.2f} Course: {:.2f} Location: {:.6f},{:.6f}".format(
            as_float(values, "speed"), as_float(values, "distance"), as_float(values, "course"),
            as_float(values, "location_lat"), as_float(values, "location_lon"),
        )
    )

    if "rudder_ok" not in values:
        lines.append("Rudder: (not sent by this firmware)")
    elif as_int(values, "rudder_ok"):
        lines.append(f"Rudder: {as_float(values, 'rudder_angle'):.1f} (180 = centered)")
    else:
        lines.append("Rudder: no data")

    if "wind_ok" not in values:
        lines.append("Wind: (not sent by this firmware)")
    elif as_int(values, "wind_ok"):
        temp = f"{as_float(values, 'air_temp_c'):.1f} C" if as_int(values, "temp_ok") else "no data"
        lines.append(f"Wind: {as_float(values, 'awa'):.1f} deg  {as_float(values, 'aws_kn'):.2f} kn  Temp: {temp}")
    else:
        lines.append("Wind: no data")

    if "true_wind_ok" not in values:
        pass
    elif as_int(values, "true_wind_ok"):
        lines.append(
            "True wind: {:.1f} deg  {:.2f} kn  (from {:.2f} kn SOG)".format(
                as_float(values, "twa"), as_float(values, "tws_kn"), as_float(values, "speed"),
            )
        )
    elif as_int(values, "wind_ok"):
        lines.append("True wind: no GPS fix - no boat speed to subtract")
    else:
        lines.append("True wind: no data")

    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(
        description="Listen for ~APDAT broadcasts and print a live-updating snapshot, "
        "like the controller's telnet 'p' command but continuously refreshed."
    )
    parser.add_argument("--port", type=int, default=8888, help="UDP port to listen on (default: 8888)")
    parser.add_argument("--bind", default="", help="address to bind to (default: all interfaces)")
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((args.bind, args.port))

    packet_count = 0
    sys.stdout.write("\033[2J")
    try:
        while True:
            data, addr = sock.recvfrom(65535)
            values = parse_apdat(data.decode("ascii", errors="replace"))
            if values is None:
                continue
            packet_count += 1
            output = render(values, addr, packet_count, time.time())
            sys.stdout.write("\033[H\033[J")
            sys.stdout.write(output + "\n")
            sys.stdout.flush()
    except KeyboardInterrupt:
        print()


if __name__ == "__main__":
    main()
