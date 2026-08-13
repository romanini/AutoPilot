# Controller enclosure

The case for the **controller unit** — the box that lives below decks and runs
the autopilot. Nothing is designed yet; this directory is a placeholder that
records what the enclosure has to do.

## What goes inside

Two boards, bolted together and joined by a single 2×10 ribbon:

| Board | What it contributes to the enclosure |
|---|---|
| [`circuit/Controller/`](../../circuit/Controller/README.md) | Nano ESP32 (Wi-Fi AP), BNO085 IMU, GPS breakout, 2×4 motor header, kill-switch JST |
| [`circuit/Controller-12-volt-power/`](../../circuit/Controller-12-volt-power/README.md) | 12 V entry block, Garmin NMEA connector, USB-C, two 5×20 mm fuse holders |

See [Inside the controller unit](../../circuit/README.md#inside-the-controller-unit)
for how the two boards and the outside world connect.

## What the enclosure has to do

**Be transparent to RF.** The controller board *is* the `SoberPilot` access
point — the display, both sensors and the navigator all reach it through the
walls of this box. Plastic, not metal, and no foil-lined conduit around it. The
masthead wind sensor is the longest link in the system and has a whole mast
between it and this box.

**Hold a known, repeatable attitude.** The BNO085 inside is the boat's compass,
so the enclosure's orientation *is* the heading reference. It wants to sit level
and square to the centreline, on a mount that cannot creep or be refitted a few
degrees off. It does not have to be perfect — `controller/compass.ino` carries a
`COMPASS_MOUNT_OFFSET_DEG` trim (18° as of the last bench calibration) — but
whatever offset is dialled in has to stay true, so the box should only bolt down
one way.

**Keep iron and magnets away from the IMU.** Stainless or nylon fasteners, and
give the mounting location a wide berth from speakers, DC cabling runs and
anything with a motor in it.

**Take five cable entries**, all on the power board's side except the motor
lead: 12 V supply, the 7-conductor Garmin NMEA-0183 loom, USB-C, the 2×4 motor
cable out to the driver, and the 2-pin kill switch. Glands or a gasketed entry
plate — this is a dry location but a bilge is never guaranteed dry.

**Leave the fuses reachable.** The Acc and Garmin 5×20 mm holders are the only
serviceable parts in the unit; blowing one should not mean unbolting the box.

**Leave a USB port reachable**, or accept opening the case to reflash. Both
boards break out USB.

An open question worth settling before the box is drawn: the **GPS is on the
controller board**, so wherever this unit mounts needs sky view, or the design
needs to grow a connector for an external antenna.

## Status

Placeholder only — no FreeCAD documents and no STLs yet. The
[wind sensor](../wind/README.md) is the one enclosure that is finished, and its
directory is the pattern to follow: FreeCAD sources, printable STLs, and a
build write-up.

```
cad/controller/
├── README.md     this file
├── FreeCad/      FreeCAD sources        (empty)
└── 3D-Parts/     STLs — print from here (empty)
```

---

One of the [AutoPilot enclosures](../README.md) — that README covers the layout
convention and the constraints every case shares.
