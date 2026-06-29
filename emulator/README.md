# Garmin emulator + NMEA test harness

A Python stand-in for the Garmin GPSMAP 276c on the controller's Garmin UART, plus
the authoritative Python implementation of the NMEA wire contract. Implements
step 2 of [`../.claude/docs/RouteImplementationPlan.md`](../.claude/docs/RouteImplementationPlan.md);
the wire contract it follows is [`../.claude/docs/WireContract.md`](../.claude/docs/WireContract.md).

It is a **serial** device, not an IP endpoint: it talks to the controller over the
same 4800-8N1 UART the real 276c uses, via the FTDI tap on the "Garmin In" JST.

## Modules

| File | Role |
|---|---|
| `nmea.py` | Build + XOR-checksum + parse for `RMC/GGA/WPL/RTE/RMB/XTE/BOD`. The authoritative Python side of the wire contract — its pytest suite pins it byte-for-byte to the controller's C checksums, so the two implementations can't drift. |
| `geo.py` | Great-circle bearing / distance / cross-track / dead-reckoning for the nav simulation. |
| `garmin_emulator.py` | The 276c state machine: idle `RMC/GGA`, then on command a route (`WPL`+`RTE`) and a cyclic `RMB`(+`XTE`+`BOD`) advancing along legs to a terminal `RMB` status `V`. Also **reads back** what the controller writes (the `~APTX`-pushed route) and parses it. Transport-agnostic core + a pyserial CLI + a `--dry-run` (stdout) mode. |
| `tests/` | `pytest` suites for `nmea.py` and `geo.py`. No hardware needed. |

## Setup

```bash
cd emulator
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
```

## Run the tests (no hardware)

```bash
.venv/bin/python -m pytest -q
```

## Dry-run the emulator (no hardware)

Prints the NMEA stream to stdout — idle, then navigate, then route end:

```bash
.venv/bin/python garmin_emulator.py --dry-run --navigate-after 3 --speed 6
```

(Crank `--speed` and shrink `--interval` to fast-forward through the legs.)

## Run against the real controller (Tier 2 — FTDI tap)

Wire the 5 V FTDI to the 12-volt board's **"Garmin In" JST** (the buffers
level-shift to the ESP32; do **not** tap the bare A0/A1 pins):

| FTDI wire | JST "Garmin In" pin |
|---|---|
| black (GND) | pin 7 (GND) |
| orange (TXD) | pin 1 (`Tx/A`) — controller RX-in; emulator drives NMEA in |
| yellow (RXD) | pin 2 (`Rx/A`) — controller TX-out; emulator reads `~APTX` writes |
| red (VCC) | leave open |

Then:

```bash
.venv/bin/python garmin_emulator.py --port /dev/cu.usbserial-XXXX --navigate-after 3
# find the port with: ls /dev/cu.usbserial-* (macOS)
```

What to expect, cross-checked from the **Navigator** (the SoberPilot Wi-Fi client —
the Mac is not on that network):

- The controller relays the route/nav sentences it receives as `~APRX` on UDP 8888
  (`WPL/RTE/RMB/XTE/BOD`), and **drops** the idle `RMC/GGA`. Sniff 8888 on the
  Navigator to confirm (see the plan's §1a test C).
- When the OpenCPN plugin pushes a route (`~APTX`), the controller writes it to the
  Garmin UART; the emulator's read-back log (`<- readback ...` on stderr) shows the
  parsed lines — this is the OpenCPN→Garmin round-trip verifier.

## Notes

- The emulator core (`GarminEmulator`) takes plain `writer`/`reader` callables, so
  it can be driven without serial (that's how the dry-run and future scenario tests
  work). The `scenarios/` and `opencpn_stub.py` pieces from plan §4 land with the
  later steps (3–6) that need them.
- `nmea.py` and `geo.py` are pure-Python and have no runtime dependency on
  `pyserial`; only the serial CLI imports it (lazily).
