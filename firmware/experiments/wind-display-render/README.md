# `wind-display` host renderer

Compiles the cockpit wind display's **actual drawing code** — `dial.ino` and
the layout half of `screen.ino` — on the Mac, against a framebuffer subclass of
`Adafruit_SPITFT` and the real `Adafruit_GFX.cpp`, and writes PNGs of what the
panel would show. No hardware, no flashing, no squinting at a 3.5" screen on a
bench.

```bash
./render.sh                # every scenario -> out/*.png, plus the repaint check
./render.sh wind nolink    # just those two
```

`GFX_LIB=/path/to/Adafruit_GFX_Library ./render.sh` if the library is not in
`~/dev/arduino/libraries`.

## Why it exists

Three real defects were found with it before the sketch ever ran on hardware,
and none of them would have been obvious from reading the code:

- **Text silently wrapping.** GFX wraps by default, so `"APPARENT"` — 100 px at
  9pt, in a 90 px canvas — folded onto a second line *inside the same box* and
  came out as two overlapping half-rows. It reads as a font bug. `draw_text()`
  now calls `setTextWrap(false)`, and `measure.cpp` prints real string widths so
  boxes can be sized against measurements instead of guesses.
- **The dial message overflowing the ring.** `"masthead not reporting"` is
  269 px at 12pt against the 216 px that fits inside the ring, so it was
  centre-clipped at both ends. The sub-line is 9pt now.
- **The pointer erase leaking.** Erasing a needle by refilling a slightly larger
  triangle looks obviously correct and is not: `fillTriangle` rasterises from
  integer vertices, so re-rounding them shifts scanline spans and the grown fill
  misses occasional edge pixels near the tip. At 1 Hz that accumulates into a
  spray of stale amber and cyan specks across the dial. The fix is to erase the
  vertex bounding box, which is a superset however the rasteriser rounds.

- **The true arrow invisible whenever the boat was stopped.** It is smaller than
  the apparent arrow in every dimension, so drawn underneath it did not overlap,
  it disappeared — and the two coincide exactly at zero boat speed, which is
  every bench test. Painting it last fixed it; the `overlap` check now enforces
  the containment that makes that readable.

That is why the script does more than draw pictures.

## The checks

Two of the "scenarios" produce no image at all:

**`boxes`** brute-forces every pointer bounding box over all 3600 tenth-degree
angles and asserts none reaches the coloured ring. Tiles are filled edge to
edge, and the ring is painted once at boot and never repainted, so a box that
overran it would notch it permanently. The worst case is r=114.1 against the
interior's 116 — a margin of under two pixels, which is exactly why it is
asserted rather than eyeballed.

**`overlap`** paints the two arrows coincident — the state whenever the boat is
stopped — at every whole degree, and fails if a single cyan pixel touches the
background. The true arrow lies strictly inside the apparent one's footprint, so
it is visible only because it is painted last *and* the amber still shows a
margin all the way round; this checks the second half of that, which is the part
a change to either arrow's proportions could quietly break.

**`cost`** reports what one wind update costs the panel: how many contiguous
pushes it makes and how many pixels they carry, for a range of angle steps.
Typical is 2–4 tiles and 5–18k pixels, 3–12 ms of SPI at 24 MHz.

## The scenarios

| Scenario | What it shows |
|----------|---------------|
| `wind` | A normal reading: both pointers, all four numbers |
| `nolink` | No `~APDAT` inside the receive timeout |
| `nowind` | Link fine, controller's `isWindOk()` false |
| `nofix` | Apparent wind fine, true wind unavailable (no GPS fix) |
| `sweep` | 91 frames of wind swinging right round the boat |
| `sweepref` | One clean frame at the sweep's final reading |
| `recover` | wind → link lost → link back |
| `recoverref` | One clean frame at the recovery's final reading |
| `coincide` | True wind equal to apparent — the boat-stopped case |
| `boxes` | Check, no image: no pointer box ever reaches the ring |
| `overlap` | Check, no image: the true arrow stays inside the apparent one |
| `cost` | Check, no image: tiles and pixels pushed per wind update |

The last four are the **incremental-repaint proof**: `render.sh` diffs `sweep`
against `sweepref` and `recover` against `recoverref` and requires **0**
differing pixels. That is the property the display actually depends on — it
never repaints the whole dial, so a hundred incremental frames have to land
exactly where a single from-scratch paint would. Rerun it after any change to
`dial.ino`.

Those four run against a **second build with the screen's damping switched off**
(`-DWIND_DAMPING_TAU_MS=0`). A first-order filter converges asymptotically, so a
swept-to value and a snapped-to value differ in the last fraction of a degree
forever — enough to move a rounded vertex by a pixel and drown the thing being
tested. The picture scenarios use the firmware's real damping.

## What is faked, and what is not

Not faked: `Adafruit_GFX.cpp` itself, the real fonts, `dial.ino` verbatim, and
everything in `screen.ino` below `setup_screen()`.

Faked (`shim/`): `Arduino.h`, `Print.h`, `SPI.h` and the BusIO headers, reduced
to what GFX touches; `Adafruit_SPITFT` as a framebuffer with a `drawPixel`; and
`main.cpp`'s scripted stand-in for the parsed `~APDAT` mirror. `render.sh`
strips `setup_screen()` out of `screen.ino` because panel detection and SPI
bring-up are the one part that genuinely needs the hardware.

So this proves **layout and repaint logic**, not the panel driver, the Wi-Fi
path or the parser. Colours are RGB565 converted straight to RGB888, so they
are the values the panel is sent, not what a transflective LCD makes of them in
daylight.
