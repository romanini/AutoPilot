#!/usr/bin/env python3
# Bridges fields carried on the controller's ~APDAT broadcast (UDP 8888, see
# firmware/Arduino/controller/publish.ino) into standard NMEA0183 sentences
# sent over UDP: wind as MWV, rudder angle as RSA. This lets OpenCPN's stock
# Dashboard plugin display both on its own gauges with no new plugin code -
# see the "Wind display in OpenCPN" section of navigator/README.md for how to
# point an OpenCPN connection at this.
#
# Cadence note: this can only update as fast as ~APDAT is broadcast (1 Hz by
# default), even though the wind and rudder boards sample faster (5 Hz / 50 Hz)
# - the controller only merges and re-broadcasts once per PUBLISH_INTERVAL.
# Raise that in publish.ino if a gauge feels sluggish; nothing here needs to
# change to benefit from it.
#
# Rudder sign convention: ~APDAT's rudder_angle is 0-360 with 180 = centered
# (see rudder/angle.ino), so it's converted here to RSA's signed degrees via
# `angle - 180`. Which physical direction (port/starboard) that comes out
# positive isn't pinned down anywhere in the firmware - the rudder feedback
# isn't wired into steering yet, so nothing has needed to fix the sign before
# now. If the Dashboard rudder gauge swings the wrong way when you turn the
# wheel, negate the value in rsa_sentence()'s caller below.
#
# Usage: apdat-nmea-bridge.py [--listen-port 8888] [--nmea-host 127.0.0.1] [--nmea-port 10110]

import argparse
import socket
import sys

# Same field order as apdat-monitor.py - matches the controller's snprintf()
# field order exactly (fields are only ever appended, never reordered).
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

# The wind/rudder fields are trailing/optional, same convention every ~APDAT
# consumer in this project follows - a frame without them is just skipped,
# not an error, so a bridge running against older controller firmware doesn't
# spam the log. Checked independently since a firmware version could have one
# without the other.
REQUIRED_WIND = ("awa", "aws_kn", "wind_ok", "twa", "tws_kn", "true_wind_ok")
REQUIRED_RUDDER = ("rudder_angle", "rudder_ok")


def parse_apdat(line):
    line = line.strip()
    if not line.startswith("~APDAT,") or not line.endswith("$"):
        return None
    parts = line[len("~APDAT,"):-1].split(",")
    return dict(zip(FIELDS, parts))


def as_float(values, name, default=0.0):
    try:
        return float(values[name])
    except (KeyError, ValueError):
        return default


def as_bool(values, name):
    try:
        return bool(int(float(values[name])))
    except (KeyError, ValueError):
        return False


def checksum(sentence_body):
    cksum = 0
    for ch in sentence_body:
        cksum ^= ord(ch)
    return f"{cksum:02X}"


def mwv_sentence(angle, reference, speed_kn, valid):
    # reference: 'R' = relative (apparent), 'T' = theoretical (true) - NMEA's
    # own vocabulary for exactly the apparent/true distinction ~APDAT carries.
    # Status 'V' (invalid) rather than an ok-looking 'A' with stale numbers
    # when the source flag is false - same "no data beats a frozen reading"
    # rule isWindOk()/isTrueWindOk() enforce everywhere else in this project.
    status = "A" if valid else "V"
    body = f"WIMWV,{angle:.1f},{reference},{speed_kn:.1f},N,{status}"
    return f"${body}*{checksum(body)}\r\n"


def rsa_sentence(angle_deg, valid):
    # Single-rudder boat: only the first angle/status pair is meaningful, the
    # second (port rudder, for twin-rudder boats) is left empty rather than
    # duplicating the same value, so nothing downstream mistakes this for a
    # boat with independent port/starboard rudders.
    status = "A" if valid else "V"
    body = f"IIRSA,{angle_deg:.1f},{status},,"
    return f"${body}*{checksum(body)}\r\n"


def build_wind_sentences(values):
    apparent = mwv_sentence(as_float(values, "awa"), "R", as_float(values, "aws_kn"), as_bool(values, "wind_ok"))
    true_wind = mwv_sentence(as_float(values, "twa"), "T", as_float(values, "tws_kn"), as_bool(values, "true_wind_ok"))
    return apparent, true_wind


def build_rudder_sentence(values):
    signed_angle = as_float(values, "rudder_angle") - 180.0
    return rsa_sentence(signed_angle, as_bool(values, "rudder_ok"))


def main():
    parser = argparse.ArgumentParser(
        description="Bridge ~APDAT wind and rudder fields to NMEA0183 MWV/RSA sentences "
        "over UDP, so OpenCPN's Dashboard plugin can display them with no new plugin code."
    )
    parser.add_argument("--listen-port", type=int, default=8888, help="UDP port to listen for ~APDAT on (default: 8888)")
    parser.add_argument("--bind", default="", help="address to bind the listener to (default: all interfaces)")
    parser.add_argument("--nmea-host", default="127.0.0.1", help="where to send NMEA sentences (default: 127.0.0.1, i.e. this machine)")
    parser.add_argument("--nmea-port", type=int, default=10110, help="UDP port to send NMEA sentences to (default: 10110)")
    args = parser.parse_args()

    listen_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listen_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listen_sock.bind((args.bind, args.listen_port))

    send_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    nmea_dest = (args.nmea_host, args.nmea_port)

    warned_missing_wind = False
    warned_missing_rudder = False
    while True:
        data, _addr = listen_sock.recvfrom(65535)
        values = parse_apdat(data.decode("ascii", errors="replace"))
        if values is None:
            continue

        sentences = []

        if all(field in values for field in REQUIRED_WIND):
            sentences.extend(build_wind_sentences(values))
        elif not warned_missing_wind:
            print("~APDAT frame is missing wind fields - is the controller firmware old?", file=sys.stderr)
            warned_missing_wind = True

        if all(field in values for field in REQUIRED_RUDDER):
            sentences.append(build_rudder_sentence(values))
        elif not warned_missing_rudder:
            print("~APDAT frame is missing rudder fields - is the controller firmware old?", file=sys.stderr)
            warned_missing_rudder = True

        for sentence in sentences:
            send_sock.sendto(sentence.encode("ascii"), nmea_dest)


if __name__ == "__main__":
    main()
