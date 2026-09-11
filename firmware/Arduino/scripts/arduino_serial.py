#!/usr/bin/env python3
"""Watch the serial output of a Nano ESP32 sketch by friendly board name.

The Arduino IDE's Serial Monitor works fine, but it holds the port open --
which blocks DFU and makes arduino_upload.py refuse to flash. This is the
terminal equivalent: run it in its own tab, Ctrl-C when you want to upload.

Board names come from boards.json (see arduino_link.py), so you never have to
work out which /dev/cu.usbmodem... belongs to which board.

Usage:
    arduino_serial.py <controller|display|rudder|wind> [--baud N] [--timestamps]

The Nano ESP32's Serial is USB CDC, so the baud rate is cosmetic, but each
sketch's Serial.begin() rate is used by default anyway.
"""
import argparse
import subprocess
import sys
import time
from datetime import datetime

import serial

from arduino_link import connected_arduinos, load_boards

# What each sketch passes to Serial.begin(); anything else falls back to 115200.
SKETCH_BAUD = {
    "controller": 38400,
    "display": 38400,
    "rudder": 115200,
    "wind": 115200,
}
DEFAULT_BAUD = 115200


def port_busy(device_path):
    result = subprocess.run(["lsof", "-t", device_path], capture_output=True, text=True)
    return result.stdout.strip() != ""


def find_port(serial_number):
    return next((p for p in connected_arduinos() if p.serial_number == serial_number), None)


def wait_for_port(serial_number, timeout):
    """Wait for a board to (re-)enumerate, e.g. after a reset or a reflash."""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        match = find_port(serial_number)
        if match:
            return match
        time.sleep(0.5)
    return None


def stream(device_path, baud, timestamps):
    """Print lines from the port until it goes away or the user hits Ctrl-C."""
    with serial.Serial(device_path, baud, timeout=0.2) as port:
        buf = b""
        while True:
            chunk = port.read(4096)
            if chunk:
                buf += chunk
                *lines, buf = buf.split(b"\n")
                for line in lines:
                    text = line.decode("utf-8", errors="replace").rstrip("\r")
                    if timestamps:
                        text = f"{datetime.now():%H:%M:%S.%f}"[:-3] + f"  {text}"
                    print(text, flush=True)


def main():
    parser = argparse.ArgumentParser(description="Watch a board's serial output by name.")
    parser.add_argument("name", help="friendly board name from boards.json")
    parser.add_argument("--baud", type=int, help="override the sketch's Serial.begin() rate")
    parser.add_argument("--timestamps", action="store_true", help="prefix each line with the local time")
    parser.add_argument(
        "--no-reconnect",
        action="store_true",
        help="exit when the board disconnects instead of waiting for it to come back",
    )
    args = parser.parse_args()

    boards = load_boards()
    by_name = {v: k for k, v in boards.items()}
    serial_number = by_name.get(args.name)
    if not serial_number:
        sys.exit(f"'{args.name}' is not a registered board. Run arduino_link.py list/register first.")

    match = find_port(serial_number)
    if not match:
        sys.exit(f"'{args.name}' (serial {serial_number}) is not currently connected.")

    if port_busy(match.device):
        sys.exit(
            f"{match.device} is held open by another process (probably an Arduino IDE "
            f"Serial Monitor tab, or another arduino_serial.py, on '{args.name}')."
        )

    baud = args.baud or SKETCH_BAUD.get(args.name, DEFAULT_BAUD)
    print(
        f"Listening to '{args.name}' on {match.device} at {baud} baud -- Ctrl-C to stop.",
        file=sys.stderr,
    )

    while True:
        try:
            stream(match.device, baud, args.timestamps)
        except KeyboardInterrupt:
            print("", file=sys.stderr)
            return
        except serial.SerialException as exc:
            if args.no_reconnect:
                sys.exit(str(exc))
            # The board drops off the USB bus whenever it resets or is reflashed.
            print(f"'{args.name}' disconnected, waiting for it to come back ...", file=sys.stderr)

        try:
            match = wait_for_port(serial_number, timeout=60)
        except KeyboardInterrupt:
            print("", file=sys.stderr)
            return
        if not match:
            sys.exit(f"'{args.name}' did not come back within 60s.")
        # Give the CDC endpoint a moment to settle before reopening it.
        time.sleep(0.5)
        print(f"Reconnected to '{args.name}' on {match.device}.", file=sys.stderr)


if __name__ == "__main__":
    main()
