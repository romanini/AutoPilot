#!/usr/bin/env python3
"""Compile and flash a Nano ESP32 sketch by friendly board name, from the CLI.

The Arduino IDE's plain "Upload" always flashes over DFU (dfu-util), and
dfu-util enumerates every DFU-capable device on the bus rather than the one
port you picked in the IDE -- so with more than one Nano ESP32 connected you
get "More than one DFU capable USB device found!" no matter what. Selecting
the esptool "Programmer" doesn't change this either: that only wires up
Sketch > Upload Using Programmer, not the regular Upload button.

This script disambiguates by passing dfu-util the board's actual USB serial
number (-S), so any number of boards can stay connected at once. It looks up
the serial for a friendly name in boards.json (see arduino_link.py).

Usage:
    arduino_upload.py <controller|display|rudder> [sketch-dir]

Before flashing, it checks whether anything (typically an Arduino IDE Serial
Monitor tab) already has the board's port open -- that blocks DFU access at
the USB level and otherwise fails with a cryptic LIBUSB_ERROR_PIPE.
"""
import glob
import subprocess
import sys
from pathlib import Path

from arduino_link import connected_arduinos, load_boards

SCRIPT_DIR = Path(__file__).resolve().parent
ARDUINO_DIR = SCRIPT_DIR.parent
ARDUINO_CLI = Path(
    "/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli"
)
FQBN = "arduino:esp32:nano_nora"
DFU_VID_PID = "2341:0070"


def find_dfu_util():
    candidates = sorted(
        glob.glob(str(Path.home() / "Library/Arduino15/packages/arduino/tools/dfu-util/*/dfu-util"))
    )
    if not candidates:
        sys.exit("Could not find dfu-util under ~/Library/Arduino15 -- is the esp32 core installed?")
    return candidates[-1]


def port_busy(device_path):
    result = subprocess.run(["lsof", "-t", device_path], capture_output=True, text=True)
    return result.stdout.strip() != ""


def main():
    if len(sys.argv) < 2:
        sys.exit(f"usage: {sys.argv[0]} <name> [sketch-dir]")
    name = sys.argv[1]
    sketch_dir = Path(sys.argv[2]) if len(sys.argv) > 2 else ARDUINO_DIR / name
    if not sketch_dir.is_dir():
        sys.exit(f"No sketch directory at {sketch_dir}")

    boards = load_boards()
    by_name = {v: k for k, v in boards.items()}
    serial = by_name.get(name)
    if not serial:
        sys.exit(f"'{name}' is not a registered board. Run arduino_link.py list/register first.")

    match = next((p for p in connected_arduinos() if p.serial_number == serial), None)
    if not match:
        sys.exit(f"'{name}' (serial {serial}) is not currently connected.")

    if port_busy(match.device):
        sys.exit(
            f"{match.device} is held open by another process (probably an Arduino IDE "
            f"Serial Monitor tab on '{name}'). Close that Serial Monitor and try again."
        )

    print(f"Compiling {sketch_dir.name} for {FQBN} ...")
    subprocess.run(
        [str(ARDUINO_CLI), "compile", "--profile", "nano", "--export-binaries", "."],
        cwd=sketch_dir,
        check=True,
    )

    bin_path = sketch_dir / "build" / "arduino.esp32.nano_nora" / f"{sketch_dir.name}.ino.bin"
    if not bin_path.exists():
        sys.exit(f"Expected compiled binary not found at {bin_path}")

    print(f"Flashing '{name}' (serial {serial}) via dfu-util ...")
    subprocess.run(
        [find_dfu_util(), "--device", DFU_VID_PID, "-S", serial, "-D", str(bin_path), "-Q"],
        check=True,
    )
    print(f"Done: '{name}' flashed.")


if __name__ == "__main__":
    main()
