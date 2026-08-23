# Enclosures

The case each AutoPilot unit lives in, one directory per unit. Only the wind
sensor is designed and printable; the other three record what their enclosure
has to do, ready for someone to draw it.

| Unit | Enclosure | Status | Boards inside |
|---|---|---|---|
| Wind sensor | [`wind/`](wind/README.md) | **Built** — FreeCAD sources and a full assembly guide | [`circuit/Sensor-Wind/`](../circuit/Sensor-Wind/README.md) |
| Controller | [`controller/`](controller/README.md) | Placeholder | [`circuit/Controller/`](../circuit/Controller/README.md) + [`circuit/Controller-12-volt-power/`](../circuit/Controller-12-volt-power/README.md) |
| Display | [`display/`](display/README.md) | Placeholder | [`circuit/Display/`](../circuit/Display/README.md) + [`circuit/Display-Button/`](../circuit/Display-Button/README.md) + [`circuit/Display-LCD/`](../circuit/Display-LCD/README.md) |
| Rudder sensor | [`rudder/`](rudder/README.md) | Placeholder | [`circuit/Sensor-Rudder/`](../circuit/Sensor-Rudder/README.md) |

## Layout convention

Each unit directory follows the same shape:

```
<unit>/
├── README.md     what the enclosure has to do, and what goes inside
└── *.FCStd       FreeCAD documents — the editable source
```

**FreeCAD documents are the only source of truth. No STLs are kept in the
repo** — export a mesh from the `.FCStd` when you are about to print, and leave
it out of git. Anything checked in would be a stale copy of a model that has
moved on.

The [wind sensor](wind/README.md) is the worked example: it adds an
`Assembly.md` with the step-by-step build, the shopping list, and the print
settings.

## What every one of these cases has in common

- **Plastic, not metal.** Every unit is on the `SoberPilot` Wi-Fi — the
  controller hosts the network and the other three join it as stations — so no
  case may act as a shield. The controller's is the critical one: everything
  else reaches it through those walls.
- **PETG or ASA, in a light colour.** Not PLA, which creeps and sags in the sun.
  Dark filament heats enough at a masthead or in a cockpit to go soft.
- **Sealed, with the cable entry as the weak point.** These live at a masthead,
  in a cockpit and in a quadrant locker. Only the controller's box is anywhere
  dry, and a bilge is never guaranteed dry either.
- **No iron near a magnet or a compass.** The controller carries the IMU that
  *is* the boat's heading reference, and both sensors read a magnet through a
  small air gap.

The board dimensions, connector positions and mounting holes all come from the
EasyEDA exports in [`circuit/`](../circuit/README.md) — each board directory
ships a DXF and a 3D model of the assembled board for exactly this purpose.
