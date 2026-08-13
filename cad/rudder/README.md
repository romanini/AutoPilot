# Rudder sensor enclosure

The case and bracket for the **rudder angle sensor** — the unit down by the
quadrant that tells the controller where the rudder actually is. The boat is
wheel-steered, so without this the controller is steering blind. Nothing is
designed yet; this directory is a placeholder that records what the enclosure
has to do.

## What goes inside

One board, and one magnet that is not on it:

| Part | What it contributes to the enclosure |
|---|---|
| [`circuit/Sensor-Rudder/`](../../circuit/Sensor-Rudder/README.md) | Nano ESP32 + AS5600L encoder, 2-pin 3.5 mm terminal block for 12 V |
| — | A **diametrically magnetised** magnet, mounted on the rudder stock itself |

## What the enclosure has to do

This is really two problems: a sealed box for the board, and a bracket that
holds the AS5600L in a fixed relationship to a rotating magnet.

**Put the magnet on the axis.** The AS5600L reads the *direction* of the field
across its face, so the magnet has to sit on the rudder stock's axis of rotation
and turn with it, with the chip stationary and centred over it. Off-axis is the
one error the firmware cannot calibrate away.

**Hold a small, constant air gap.** A couple of millimetres, held rigid — a
bracket that flexes as the quadrant loads up turns rudder force into rudder
angle. The board reports the AS5600L's own magnet-detect flag in every
`~APRUD,<angle>,<magnet_ok>$` packet, so the gap can be checked live from a
laptop on `SoberPilot` rather than by eye.

**Stay rigid rather than precise.** Centring is a runtime command
(`~APCMD,z$` with the rudder amidships, persisted in NVS), so the bracket does
not have to be angularly accurate — it just must not move afterwards. Nothing
ferrous near the magnet.

**Survive a wet locker.** A lazarette or quadrant space is the wettest place any
of these units lives short of the masthead. Sealed box, gasketed lid, gland for
the single 12 V pair — which is the whole point of putting this unit on Wi-Fi
rather than running a signal cable forward.

**Be transparent to RF.** This is a station on `SoberPilot`, usually as far from
the controller as anything below decks gets, often with a bulkhead or two in
between. Plastic case, and worth checking link quality at the chosen mounting
spot before committing to it.

## Status

Placeholder only — no FreeCAD documents and no STLs yet. The
[wind sensor](../wind/README.md) is the one enclosure that is finished, and its
directory is the pattern to follow: IGES/FreeCAD sources, printable STLs, and a
build write-up.

```
cad/rudder/
├── README.md     this file
├── FreeCad/      FreeCAD sources        (empty)
└── 3D-Parts/     STLs — print from here (empty)
```
