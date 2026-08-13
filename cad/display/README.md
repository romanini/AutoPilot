# Display enclosure

The case for the **cockpit head unit** — the screen and buttons the helm
actually touches. Nothing is designed yet; this directory is a placeholder that
records what the enclosure has to do.

## What goes inside

A stack of two or three boards plus a panel and a battery:

| Board | What it contributes to the enclosure |
|---|---|
| [`circuit/Display/`](../../circuit/Display/README.md) | Nano ESP32, TP5100 charger, TPS61023 boost, buzzer, JST-PH to the LiPo |
| [`circuit/Display-Button/`](../../circuit/Display-Button/README.md) | Five tactile buttons and the illuminated power switch — both need panel cutouts |
| [`circuit/Display-LCD/`](../../circuit/Display-LCD/README.md) | Optional ST7365P carrier with its 40-pin FPC tail |
| — | A 3.5" 320×480 panel: either an HX8357 module or the VIEWE ST7365P |
| — | A LiPo cell — the only rail that actually runs the unit |

They chain mainboard → 2×10 ribbon → button board → 8-pin JST-XH → panel, so
the **stack height and the ribbon service loops set the case depth**. See
[Inside the display unit](../../circuit/README.md#inside-the-display-unit).

## What the enclosure has to do

**Survive the cockpit.** Rain, spray, sun and the occasional boot. Gasketed lid,
UV-stable material, and a light colour — this is the one box that sits in direct
sun all day.

**Frame the screen.** A window over the 3.5" 320×480 active area, with a bezel
that hides the panel border without eating pixels. The two supported panels do
not share an outline: the HX8357 module bolts up as a board, while the ST7365P
is a bare panel on a flex tail into the carrier, which needs clearance for the
FPC and its bend radius. Either the case supports both, or it commits to one and
says so.

**Carry six controls.** Five button plungers or a membrane over the tactile
switches — **Port, Enable, Mode, Starboard, Tack** — plus a cutout for the
illuminated 4-pin power switch. The buttons are the part most likely to leak and
the part most often used with wet gloves, so plunger seals and travel are worth
prototyping before the rest of the case is finalised.

**Hold the battery, and let it be replaced.** The display always runs off its
LiPo — 12 V and USB only charge it — so a dead cell means a dead unit. It needs
a retained pocket, strain relief on the JST-PH lead, and a back that opens
without disturbing the panel seal.

**Be transparent to RF.** The display is a station on `SoberPilot`; no metal
bezel wrapping the ESP32's antenna.

**Decide what stays reachable.** The 12 V charge lead has to enter somehow, and
USB-C is how the unit gets reflashed — either a sealed port with a cap, or
accept opening the case.

## Status

Placeholder only — no FreeCAD documents and no STLs yet. The
[wind sensor](../wind/README.md) is the one enclosure that is finished, and its
directory is the pattern to follow: FreeCAD sources, printable STLs, and a
build write-up.

```
cad/display/
├── README.md     this file
├── FreeCad/      FreeCAD sources        (empty)
└── 3D-Parts/     STLs — print from here (empty)
```
