#!/usr/bin/env bash
# Build and run the wind-display host renderer. See README.md.
#
#   ./render.sh                  # all scenarios -> out/*.png
#   ./render.sh wind             # just one
#
# GFX_LIB may be set to point at an Adafruit_GFX_Library checkout other than the
# default sketchbook one.
set -euo pipefail
cd "$(dirname "$0")"

GFX_LIB="${GFX_LIB:-$HOME/dev/arduino/libraries/Adafruit_GFX_Library}"
SKETCH=../../Arduino/wind-display

if [ ! -f "$GFX_LIB/Adafruit_GFX.cpp" ]; then
  echo "Adafruit_GFX_Library not found at $GFX_LIB - set GFX_LIB=/path/to/it" >&2
  exit 1
fi

mkdir -p out

# screen.ino's panel bring-up talks to the real SPI drivers; everything below it
# is layout, which is what we want to exercise. Strip the bring-up and the
# driver includes, and leave the rest byte-for-byte as the firmware has it.
python3 - "$SKETCH/screen.ino" out/screen_layout.inc <<'PY'
import sys, re
src, dst = sys.argv[1], sys.argv[2]
s = open(src).read()
for inc in ['#include <SPI.h>\n', '#include "Adafruit_HX8357.h"\n',
            '#include "Adafruit_ST7365.h"\n', '#include "tft.h"\n']:
    s = s.replace(inc, '')
start = s.index('void setup_screen() {')
end = s.index('\n}\n', start) + 3
s = s[:start] + s[end:]
s = s.replace('Adafruit_SPITFT *tft = nullptr;\n', '')
s = s.replace('TftType fittedTft = TFT_AUTO;\n', '')
s = re.sub(r'static constexpr uint32_t \w+ = \d+;\n', '', s)
for a, b in [('static void initialize_displayed_values();', 'void initialize_displayed_values();'),
             ('static void initialize_displayed_values() {', 'void initialize_displayed_values() {'),
             ('static void draw_status_chrome();', 'void draw_status_chrome();'),
             ('static void draw_status_chrome() {', 'void draw_status_chrome() {')]:
    s = s.replace(a, b)
open(dst, 'w').write(s)
PY

c++ -std=c++17 -O1 -DARDUINO=200 -o out/render main.cpp "$GFX_LIB/Adafruit_GFX.cpp" \
    -I shim -I "$GFX_LIB" -I "$SKETCH" -I out

# A second build with the screen's damping switched off. The repaint checks need
# it: a first-order filter converges asymptotically, so a swept-to value and a
# snapped-to value differ in the last fraction of a degree forever, which is
# enough to move a rounded vertex by a pixel and drown the thing being tested.
c++ -std=c++17 -O1 -DARDUINO=200 -DWIND_DAMPING_TAU_MS=0 -o out/render_nodamp \
    main.cpp "$GFX_LIB/Adafruit_GFX.cpp" -I shim -I "$GFX_LIB" -I "$SKETCH" -I out
c++ -std=c++17 -O1 -DARDUINO=200 -o out/measure measure.cpp "$GFX_LIB/Adafruit_GFX.cpp" \
    -I shim -I "$GFX_LIB"

SCENARIOS="${*:-wind coincide nolink nowind nofix sweep sweepref recover recoverref}"
cd out

# "boxes" is a check, not a picture: it brute-forces every pointer box over all
# 3600 tenth-degree angles and asserts none of them reaches the coloured ring.
# Tiles are filled edge to edge, and the ring is painted once at boot and never
# repainted, so a box that overran it would notch it permanently.
if [ -z "${*:-}" ] || case " $SCENARIOS " in *" boxes "*) true;; *) false;; esac; then
  ./render boxes
  ./render overlap
  echo "cost of one wind update:"
  ./render_nodamp cost
  SCENARIOS=$(echo "$SCENARIOS" | tr ' ' '\n' | grep -vE '^(boxes|cost|overlap)$' | tr '\n' ' ')
fi

for s in $SCENARIOS; do
  # The four comparison scenarios run undamped - see the second build above.
  case "$s" in
    sweep|sweepref|recover|recoverref|cost|overlap) BIN=./render_nodamp;;
    *) BIN=./render;;
  esac
  "$BIN" "$s" > /dev/null
  python3 - "$s" <<'PY'
import struct, zlib, sys
name = sys.argv[1]
d = open(name + '.ppm', 'rb').read()
parts = d.split(b'\n', 3)
w, h = map(int, parts[1].split())
px = parts[3]
raw = b''.join(b'\x00' + px[y*w*3:(y+1)*w*3] for y in range(h))
ch = lambda t, dd: struct.pack('>I', len(dd)) + t + dd + struct.pack('>I', zlib.crc32(t+dd) & 0xffffffff)
open(name + '.png', 'wb').write(
    b'\x89PNG\r\n\x1a\n'
    + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
    + ch(b'IDAT', zlib.compress(raw))
    + ch(b'IEND', b''))
PY
  echo "out/$s.png"
done

# The incremental-repaint proof: a long sweep must land pixel-identical to a
# from-scratch paint of the same reading, and so must a link-loss recovery.
compare() {
  python3 - "$1" "$2" <<'PY'
import sys
def load(n):
    d = open(n + '.ppm', 'rb').read(); p = d.split(b'\n', 3)
    w, h = map(int, p[1].split()); return w, h, p[3]
w, h, a = load(sys.argv[1]); _, _, b = load(sys.argv[2])
n = sum(1 for i in range(0, len(a), 3) if a[i:i+3] != b[i:i+3])
print(f"  {sys.argv[2]} vs {sys.argv[1]}: {n} differing pixels" + ("  OK" if n == 0 else "  <-- LEAK"))
PY
}
case " $SCENARIOS " in *" sweep "*) echo "incremental repaint:"; compare sweepref sweep;; esac
case " $SCENARIOS " in *" recover "*) compare recoverref recover;; esac
