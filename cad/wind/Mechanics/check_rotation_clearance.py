"""
FreeCAD macro: check that a rotating part clears a stationary one.

The anemometer cups and the wind vane sweep a full circle about the Z axis, so a
single static check is not enough -- a part can be clear in one angular position
and foul 30 degrees later.  This steps the moving part right round the axis and
checks every position, then reports the tightest gap it ever gets to.

HOW TO USE (GUI)
    1. Open BottomHousing.FCStd.
    2. File -> Merge project...  and pick CupRound.FCStd (and CupWheelHub.FCStd).
       Both parts are now in one document, each still on its own origin.
    3. Select the cup, edit Data -> Placement so it sits where it really does
       when assembled on the hub.  Get this right -- it is the whole answer.
    4. Select the MOVING part(s) first, then Ctrl-click the STATIONARY part LAST.
    5. Macro -> Macros... -> add this file if it is not listed -> Execute.

Takes well under a minute.  Prints the swept envelope, any interference (with
volume and location), and otherwise the minimum clearance over a full turn.

HEADLESS
    Set DOC / MOVING / STATIC below, then:
        freecadcmd check_rotation_clearance.py
"""

import FreeCAD as App
import Part
from FreeCAD import Vector

STEPS   = 72                 # angular steps; 72 = every 5 degrees
AXIS    = Vector(0, 0, 1)
ORIGIN  = Vector(0, 0, 0)
GAP_TOL = 1e-6               # volumes below this count as "touching, not fouling"

# Headless use only.  Leave as None to take the GUI selection instead.
DOC    = None                # e.g. 'BottomHousing'
MOVING = None                # e.g. ['CupRound_0', 'CupRound_3']
STATIC = None                # e.g. 'BottomHousing'


def cyl_extent(shape):
    """(r_min, r_max, z_min, z_max) about the rotation axis."""
    pts = [v.Point for v in shape.Vertexes]
    for f in shape.Faces:                    # sample faces, not just corners
        u0, u1, v0, v1 = f.ParameterRange
        for a in range(5):
            for b in range(5):
                pts.append(f.valueAt(u0 + (u1 - u0) * a / 4.0,
                                     v0 + (v1 - v0) * b / 4.0))
    rr = [(p.x ** 2 + p.y ** 2) ** 0.5 for p in pts]
    zz = [p.z for p in pts]
    return min(rr), max(rr), min(zz), max(zz)


def run(moving_shapes, static_shape, labels=('moving', 'static')):
    mv = moving_shapes[0]
    for s in moving_shapes[1:]:
        mv = mv.fuse(s)

    r0, r1, z0, z1 = cyl_extent(mv)
    s0, s1, sz0, sz1 = cyl_extent(static_shape)
    print('  %-28s radius %8.2f .. %8.2f   z %8.2f .. %8.2f'
          % (labels[0] + ' (moving)', r0, r1, z0, z1))
    print('  %-28s radius %8.2f .. %8.2f   z %8.2f .. %8.2f'
          % (labels[1] + ' (static)', s0, s1, sz0, sz1))

    # Cheap decisive test first: if the two never share a height band, or never
    # share a radius band, no rotation can ever bring them together.
    if z1 < sz0 or z0 > sz1:
        print('  CLEAR - they do not overlap in height at all '
              '(%.2f mm apart in z).' % max(sz0 - z1, z0 - sz1))
        return
    if r1 < s0 or r0 > s1:
        print('  CLEAR - they do not overlap in radius at all.')
        return
    print('  envelopes overlap, so stepping %d positions around the axis...' % STEPS)

    worst_v, worst_a, worst_bb = 0.0, None, None
    for i in range(STEPS):
        ang = 360.0 * i / STEPS
        hit = mv.rotated(ORIGIN, AXIS, ang).common(static_shape)
        if hit.Volume > worst_v:
            worst_v, worst_a, worst_bb = hit.Volume, ang, hit.BoundBox

    if worst_v > GAP_TOL:
        b = worst_bb
        print('  *** FOULS.  Worst at %.0f degrees: %.2f mm3 of overlap' % (worst_a, worst_v))
        print('      around x[%.2f %.2f] y[%.2f %.2f] z[%.2f %.2f]'
              % (b.XMin, b.XMax, b.YMin, b.YMax, b.ZMin, b.ZMax))
        print('      -> that is how much has to come off, and where.')
        return

    best, best_a = None, None
    for i in range(STEPS):
        ang = 360.0 * i / STEPS
        try:
            d = mv.rotated(ORIGIN, AXIS, ang).distToShape(static_shape)[0]
        except Exception:
            continue
        if best is None or d < best:
            best, best_a = d, ang
    if best is None:
        print('  CLEAR (no overlap at any position).')
    else:
        print('  CLEAR.  Tightest point over a full turn: %.2f mm, at %.0f degrees.'
              % (best, best_a))


def main():
    if DOC:
        d = App.getDocument(DOC)
        run([d.getObject(n).Shape for n in MOVING], d.getObject(STATIC).Shape,
            (','.join(MOVING), STATIC))
        return
    try:
        import FreeCADGui
        sel = FreeCADGui.Selection.getSelection()
    except Exception:
        print('No GUI selection available - set DOC/MOVING/STATIC at the top instead.')
        return
    if len(sel) < 2:
        print('Select the MOVING part(s) first, then Ctrl-click the STATIONARY part LAST.')
        return
    run([o.Shape for o in sel[:-1]], sel[-1].Shape,
        (', '.join(o.Label for o in sel[:-1]), sel[-1].Label))


main()
