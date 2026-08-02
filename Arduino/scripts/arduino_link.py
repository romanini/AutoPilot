#!/usr/bin/env python3
"""Give the project's Nano ESP32 boards stable, friendly names.

macOS assigns /dev/cu.usbmodem... paths that are hard to tell apart when you
have several identical Nano ESP32 boards plugged in. This script matches each
connected board's USB serial number against Arduino/scripts/boards.json and
maintains symlinks under ~/.arduino-ports/<name> pointing at the real device
path, e.g.:

    ~/.arduino-ports/controller -> /dev/cu.usbmodem48CA432F9AEC2

Use the symlink path with arduino-cli, screen, esptool, etc. instead of the
raw /dev/cu.usbmodem... path. (The Arduino IDE's own port picker only scans
/dev/cu.*, so it will still show boards by their raw path there.)

Usage:
    arduino_link.py sync              # (default) refresh all symlinks
    arduino_link.py list              # show connected boards, registered or not
    arduino_link.py register <name>   # name the single unregistered board
    arduino_link.py register <name> <serial>   # name a specific serial
"""
import json
import sys
from pathlib import Path

import serial.tools.list_ports as list_ports

BOARDS_FILE = Path(__file__).resolve().parent / "boards.json"
LINK_DIR = Path.home() / ".arduino-ports"


def load_boards():
    if not BOARDS_FILE.exists():
        return {}
    return json.loads(BOARDS_FILE.read_text())


def save_boards(boards):
    BOARDS_FILE.write_text(json.dumps(boards, indent=2, sort_keys=True) + "\n")


def connected_arduinos():
    boards = []
    for p in list_ports.comports():
        if p.manufacturer == "Arduino" and p.serial_number:
            boards.append(p)
    return boards


def cmd_list():
    boards = load_boards()
    found = connected_arduinos()
    if not found:
        print("No Arduino boards currently connected.")
        return
    for p in found:
        name = boards.get(p.serial_number, "(unregistered)")
        print(f"{p.serial_number}  {p.device}  {p.product or ''}  -> {name}")


def cmd_register(name, serial=None):
    boards = load_boards()
    found = connected_arduinos()
    if serial is None:
        unregistered = [p for p in found if p.serial_number not in boards]
        if len(unregistered) == 0:
            print("No unregistered Arduino boards connected. Unplug others, "
                  "or pass a serial number explicitly.")
            sys.exit(1)
        if len(unregistered) > 1:
            print("Multiple unregistered boards connected, plug in only the "
                  "one you want to name, or pass its serial explicitly:")
            for p in unregistered:
                print(f"  {p.serial_number}  {p.device}")
            sys.exit(1)
        serial = unregistered[0].serial_number
    boards[serial] = name
    save_boards(boards)
    print(f"Registered {serial} as '{name}'")
    cmd_sync()


def cmd_sync():
    boards = load_boards()
    LINK_DIR.mkdir(exist_ok=True)

    # Drop stale symlinks (device unplugged or re-registered).
    live_names = set()
    for p in connected_arduinos():
        name = boards.get(p.serial_number)
        if name:
            live_names.add(name)

    for link in LINK_DIR.iterdir():
        if link.is_symlink() and link.name not in live_names:
            link.unlink()

    # (Re)create symlinks for connected, registered boards.
    for p in connected_arduinos():
        name = boards.get(p.serial_number)
        if not name:
            continue
        link = LINK_DIR / name
        target = Path(p.device)
        if link.is_symlink() and link.resolve() == target.resolve():
            continue
        if link.exists() or link.is_symlink():
            link.unlink()
        link.symlink_to(target)
        print(f"{name} -> {p.device}")


def main():
    args = sys.argv[1:]
    cmd = args[0] if args else "sync"
    if cmd == "list":
        cmd_list()
    elif cmd == "register":
        if len(args) < 2:
            print("usage: arduino_link.py register <name> [serial]")
            sys.exit(1)
        cmd_register(args[1], args[2] if len(args) > 2 else None)
    elif cmd == "sync":
        cmd_sync()
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == "__main__":
    main()
