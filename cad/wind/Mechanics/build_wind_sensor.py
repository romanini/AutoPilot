#!/usr/bin/env freecadcmd
"""
Build the AutoPilot masthead wind-sensor parts from the original IGES masters.

    cad/wind/Mechanics/IGES/*.iges   ->   cad/wind/Mechanics/FreeCad/*.FCStd
                                     ->   cad/wind/3D-Parts/*.stl

Every IGES master is converted to a FreeCAD document with an English name.  Two
of them are also modified to house the new Arduino-Nano-ESP32 PCB (octagonal,
103.25 x 40.13 mm):

    Unterteil.iges -> BottomHousing.FCStd -> bot.stl
    Oberteil.iges  -> TopCover.FCStd      -> top_1.stl

Those two documents keep a live Part-workbench boolean tree (OriginalHousing +
SnoutBody + SocketKeel, then the pockets cut out of it), so every feature stays
editable in the FreeCAD GUI.

Housing frame, unchanged from the masters:
    origin = rotation axis of the vane / cup wheel
    z = 0   = top rim of the bottom housing, where the cover lands
    +x      = the side that carries the mounting arm

The PCB is used rotated 180 deg about z from its EasyEDA orientation, which puts
the long nose over the arm and the 12 V terminal over the existing cable notch.

The arm-tube socket is slid along its OWN axis, so the same tube at the same
angle still fits -- it just needs shortening.  Its geometry is measured out of
the master at build time rather than hard-coded.

Run:  freecadcmd build_wind_sensor.py
"""

import json, math, os
import FreeCAD as App
import Part, Mesh, MeshPart
from FreeCAD import Vector, Rotation, Placement

_HERE = os.path.dirname(os.path.abspath(__file__))
_ROOT = os.path.abspath(os.path.join(_HERE, '..', '..', '..'))
IGES  = os.path.join(_HERE, 'IGES') + os.sep
FCDIR = os.path.join(_HERE, 'FreeCad') + os.sep
STL   = os.path.join(_ROOT, 'cad', 'wind', '3D-Parts') + os.sep
PCBJS = os.path.join(_ROOT, 'circuit', 'Wind', 'PCB_AP-Wind-Sensor_EasyEDA.json')

U      = 0.254                  # EasyEDA coordinate unit -> mm
HOLE_C = (4308.4125, 3276.0)    # centroid of the 4 PCB mounting holes

# ---------------------------------------------------------------- parameters
PCB_BOT   = -4.0     # board underside = existing pocket floor (unchanged)
PCB_TH    =  1.6
NANO_DROP = 14.0     # how far the Nano hangs below the board
NANO_L, NANO_W = 45.0, 20.0

CLR       =  0.40    # board edge -> pocket wall
RIB_CLR   =  0.25    # cover locating rib, outboard of the board edge
CHAM_IN   =  1.30    # cover chamber wall, inboard of the board edge
WALL      =  3.50    # pocket wall -> outer skin on the snout

FLOOR_Z   = -21.0    # snout underside.  Master's lowest point is -25.07.
# The snout floor has to be at FLOOR_Z from the inboard end of the Arduino well
# outwards, but the round body is only ~8 mm deep at that radius.  So the snout
# underside ramps back up going inboard and merges into the master's 46 deg
# cone, instead of stopping at a vertical wall (which left the well open to the
# weather).  SNOUT_X0 only has to be far enough inboard for the ramp to have
# merged; it must stay clear of the Ø31 centre bore and the PCB bosses at x=15.
SNOUT_X0    = 17.0
BLEND_X     = 23.5   # floor is flat outboard of here (= inboard end of the well)
BLEND_SLOPE =  2.0   # ramp gradient inboard of it

ARD_X     = (23.5, 72.5)      # Arduino well
ARD_Y     = 11.5
ARD_WIDE_X = 38.0             # widened to ARD_Y_WIDE inboard of here, for the cable
ARD_Y_WIDE = 14.5
ARD_Z     = -18.8             # well floor (Nano underside is at -18.0)

SOCKET_X  = 54.0     # blind end of the tube socket = the insertion stop
KEEL_X    = (30.0, 76.0)
KEEL_Y    =  8.0
KEEL_GAP  =  2.50    # material around the bore / duct
DUCT_R    =  3.25    # cable duct, carrying on inboard from the blind end
DUCT_X0   = 21.0
# The blend fills the master's arm-boss cavity, which used to be how the 12 V
# cable reached the pocket.  Put that route back explicitly: a riser on the
# master's own notch centre, from the duct up through the pocket floor.
RISER_XY  = (22.456, 0.0)
RISER_R   =  3.37
RISER_Z   = (-16.0, -3.0)

TIP_X     = (70.0, 82.0)      # solid tip block carrying the extra cover screw
TIP_Y     = 13.0
TIP_R     =  4.0
SCREW_XY  = (77.5, 0.0)
SCREW_R   =  1.25

# --- bearing carrier -------------------------------------------------------
# Lengthening the carrier's stem drops the cup wheel away from the relocated
# arm socket.  The rotating assembly hangs from the TOP (625) bearing at the
# flange end, and the speed-magnet holder sits up there under the hall sensor,
# so stretching the stem moves only the lower (695) bearing and the cup wheel
# that clamps against it -- the 1 mm magnet gap is untouched.
# The stem is a continuous taper with no parallel section, so the inserted
# length is a prism of the carrier's OWN cross-section at STEM_CUT_Z: it then
# matches the profile exactly at both ends instead of leaving a step.
STEM_EXTRA  = 5.0        # how much longer to make the stem
STEM_CUT_Z  = 18.0       # where to insert it (own frame; plain bore region)

COVER_X0       = 19.5    # cover snout starts where the board leaves the old octagon
COVER_ROOF_X0  = 28.0    # roof only outboard of the master's dome cavity (max x 27.8)
COVER_SHOULDER =  3.0
COVER_CEIL     =  3.5
COVER_ROOF     =  5.5
COVER_INSET    =  2.5    # = ROOF - SHOULDER, so the chamfer is 45 deg
RIB_Z          = -1.5    # rib bottom (board top is at -2.4)

MESH_DEV  = 0.02     # STL tessellation deviation

BIG = 400.0


# -------------------------------------------------------------------- 2D help
def ccw(poly):
    a = 0.0
    for i in range(len(poly)):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % len(poly)]
        a += x0 * y1 - x1 * y0
    return poly if a > 0 else poly[::-1]


def miter_offset(poly, d):
    """Exact miter offset of a convex polygon (positive = outward)."""
    poly = ccw(poly)
    n = len(poly)
    lines = []
    for i in range(n):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % n]
        ex, ey = x1 - x0, y1 - y0
        L = math.hypot(ex, ey)
        nx, ny = ey / L, -ex / L            # outward normal for CCW
        lines.append((nx, ny, nx * x0 + ny * y0 + d))
    out = []
    for i in range(n):
        a1, b1, c1 = lines[i - 1]
        a2, b2, c2 = lines[i]
        det = a1 * b2 - a2 * b1
        out.append(((c1 * b2 - c2 * b1) / det, (a1 * c2 - a2 * c1) / det))
    return out


def face(poly):
    pts = [Vector(x, y, 0) for x, y in ccw(poly)]
    return Part.Face(Part.makePolygon(pts + [pts[0]]))


def prism(f, z0, z1):
    return f.extrude(Vector(0, 0, z1 - z0)).translated(Vector(0, 0, z0))


def rounded_rect_face(x0, x1, y0, y1, r):
    inner = face([(x0 + r, y0 + r), (x1 - r, y0 + r), (x1 - r, y1 - r), (x0 + r, y1 - r)])
    return inner.makeOffset2D(r, 0, False, False)     # join=0 -> arcs


def solid(sh):
    """Boolean results come back as Compounds; FreeCAD's Part::Fuse chokes on
    those. Unwrap to the single Solid inside."""
    assert len(sh.Solids) == 1, 'expected 1 solid, got %d' % len(sh.Solids)
    return sh.Solids[0]


def half_x_ge(x0):
    return Part.makeBox(BIG, 2 * BIG, 2 * BIG, Vector(x0, -BIG, -BIG))


def half_x_le(x0):
    return Part.makeBox(BIG, 2 * BIG, 2 * BIG, Vector(x0 - BIG, -BIG, -BIG))


# ------------------------------------------------------------------ the board
def board_polygon():
    """New PCB outline in housing mm, rotated 180 deg so the nose points +x."""
    d = json.load(open(PCBJS, encoding='utf-8'))
    for s in d['shape']:
        f = s.split('~')
        if f[0] == 'TRACK' and f[2] == '10':
            n = [float(v) for v in f[4].split(' ')]
            p = list(zip(n[0::2], n[1::2]))[:-1]      # drop the repeated point
            # EasyEDA y grows downward -> negate it; then rotate 180 deg about z
            return ccw([(-(x - HOLE_C[0]) * U, (y - HOLE_C[1]) * U) for x, y in p])
    raise RuntimeError('no BoardOutLine track in ' + PCBJS)


# --------------------------------------------------- measure the master's tube
def fit_tube_axis(boss):
    """Recover the arm-tube bore (radius, a point on the axis, direction) from
    the original arm boss.  The IGES is all BSplines, so this fits rather than
    reading analytic surface parameters."""
    best = None
    for f in boss.Faces:
        if f.Area < 100:
            continue
        u0, u1, v0, v1 = f.ParameterRange
        P = []
        for i in range(16):
            for j in range(16):
                p = f.valueAt(u0 + (u1 - u0) * (i + .5) / 16,
                              v0 + (v1 - v0) * (j + .5) / 16)
                P.append((p.x, p.y, p.z))
        cx = sum(p[0] for p in P) / len(P)
        cy = sum(p[1] for p in P) / len(P)
        cz = sum(p[2] for p in P) / len(P)
        for k in range(0, 4001):
            th = math.radians(k * 0.01)
            dx, dz = math.cos(th), -math.sin(th)
            rr = []
            for x, y, z in P:                      # distance to the axis line
                ax, ay, az = x - cx, y - cy, z - cz
                t = ax * dx + az * dz
                rr.append(math.sqrt((ax - t * dx) ** 2 + ay ** 2 + (az - t * dz) ** 2))
            m = sum(rr) / len(rr)
            var = sum((r - m) ** 2 for r in rr) / len(rr)
            if best is None or var < best[0]:
                best = (var, m, th, (cx, cy, cz), f.BoundBox)
    var, radius, th, ctr, bb = best
    assert var < 1e-8, 'tube fit failed, variance %g' % var
    direction = Vector(math.cos(th), 0.0, -math.sin(th))
    # put the reference point on the axis at y = 0
    p = Vector(ctr[0], 0.0, ctr[2])
    return radius, p, direction, th, bb


# ------------------------------------------------------------------ documents
def new_doc(name):
    if name in App.listDocuments():
        App.closeDocument(name)
    return App.newDocument(name)


def feat(doc, name, shape, label=None):
    o = doc.addObject('Part::Feature', name)
    o.Shape = shape
    o.Label = label or name
    return o


def cut(doc, name, base, tool, label=None):
    o = doc.addObject('Part::Cut', name)
    o.Base, o.Tool = base, tool
    o.Refine = False          # Refine calls removeSplitter, which breaks these BSplines
    o.Label = label or name
    return o


def fuse(doc, name, shapes, label=None):
    """Pairwise Part::Fuse chain.  Part::MultiFuse fuses all operands in one
    OCC call and returns an invalid shape on these all-BSpline masters."""
    o = shapes[0]
    for i, s in enumerate(shapes[1:], 1):
        o2 = doc.addObject('Part::Fuse', '%s%d' % (name, i))
        o2.Base, o2.Tool = o, s
        o2.Refine = False
        o2.Label = (label or name) if i == len(shapes) - 1 else '%s step %d' % (name, i)
        o = o2
    return o


def box_obj(doc, name, lx, ly, lz, pos, label=None):
    o = doc.addObject('Part::Box', name)
    o.Length, o.Width, o.Height = lx, ly, lz
    o.Placement = Placement(Vector(*pos), Rotation())
    o.Label = label or name
    return o


def cyl_obj(doc, name, r, h, base, direction, label=None):
    o = doc.addObject('Part::Cylinder', name)
    o.Radius, o.Height = r, h
    o.Placement = Placement(Vector(*base), Rotation(Vector(0, 0, 1), direction))
    o.Label = label or name
    return o


_GUI_HEAD = ("<?xml version='1.0' encoding='utf-8'?>\n"
             "<!--\n FreeCAD Document, see https://www.freecad.org for more information...\n-->\n"
             '<Document SchemaVersion="1">\n'
             '    <ViewProviderData Count="%d">\n')
_GUI_VP = ('        <ViewProvider name="%s" expanded="0">\n'
           '            <Properties Count="2" TransientCount="0">\n'
           '                <Property name="ShowInTree" type="App::PropertyBool" status="1">\n'
           '                    <Bool value="true"/>\n'
           '                </Property>\n'
           '                <Property name="Visibility" type="App::PropertyBool" status="1">\n'
           '                    <Bool value="%s"/>\n'
           '                </Property>\n'
           '            </Properties>\n'
           '        </ViewProvider>\n')
_GUI_TAIL = ('    </ViewProviderData>\n'
             '    <Camera settings="OrthographicCamera {&#10;  viewportMapping ADJUST_CAMERA&#10;'
             '  position 25 -260 130&#10;  orientation 1 0 0  1.0471976&#10;'
             '  nearDistance 60&#10;  farDistance 500&#10;  aspectRatio 1&#10;'
             '  focalDistance 290&#10;  height 190&#10;&#10;}&#10;"/>\n'
             '</Document>\n')


def write_gui_data(path, visible):
    """freecadcmd has no GUI, so FreeCAD saves no GuiDocument.xml and every
    object loads HIDDEN in the FreeCAD GUI -- the document looks empty.  Write
    a minimal one ourselves: result objects visible, boolean operands hidden
    (which is what the GUI itself does after a boolean)."""
    import zipfile
    doc = App.getDocument(os.path.splitext(os.path.basename(path))[0])
    names = [o.Name for o in doc.Objects]
    xml = _GUI_HEAD % len(names)
    for n in names:
        xml += _GUI_VP % (n, 'true' if n in visible else 'false')
    xml += _GUI_TAIL
    with zipfile.ZipFile(path, 'a', zipfile.ZIP_DEFLATED) as z:
        z.writestr('GuiDocument.xml', xml)


def export(doc, obj, stl_name):
    doc.recompute()
    sh = obj.Shape
    assert sh.isValid(), stl_name + ' invalid'
    m = doc.addObject('Mesh::Feature', 'MeshOut')
    m.Mesh = MeshPart.meshFromShape(Shape=sh, LinearDeflection=MESH_DEV,
                                    AngularDeflection=0.35, Relative=False)
    Mesh.export([m], STL + stl_name)
    doc.removeObject(m.Name)
    print('   %-12s solids=%d  vol=%10.2f mm3  bbox x[%7.2f %7.2f] z[%7.2f %7.2f]'
          % (stl_name, len(sh.Solids), sh.Volume,
             sh.BoundBox.XMin, sh.BoundBox.XMax, sh.BoundBox.ZMin, sh.BoundBox.ZMax))
    return sh


# ============================================================== shared profiles
BP = board_polygon()
EXIST_POCKET = [(30.500, -10.000), (20.000, -20.500), (-20.000, -20.500),
                (-30.300, -9.685), (-30.300, 9.700), (-19.700, 20.300),
                (19.700, 20.300), (30.500, 9.500)]

F_RIBOUT  = face(miter_offset(BP, RIB_CLR))
F_CHAMBER = face(miter_offset(BP, -CHAM_IN))
F_TIP     = rounded_rect_face(TIP_X[0], TIP_X[1], -TIP_Y, TIP_Y, TIP_R)
F_BODY    = face(miter_offset(BP, WALL))
F_EXIST   = face(EXIST_POCKET)
F_BOARD   = face(miter_offset(BP, CLR))


def fuse_all(shapes):
    """Fuse a list of solids into one.

    Two hard-won rules, both about the IGES masters being all-BSpline:
      * always fuse SOLIDS, never faces.  Extruding a fused face yields a
        compound of overlapping solids that poisons every later boolean.
      * never call removeSplitter() on the result.  On these surfaces it
        turns a valid solid into an invalid one and inflates its volume."""
    r = shapes[0]
    for s in shapes[1:]:
        r = r.fuse(s)
    assert r.isValid() and len(r.Solids) == 1, \
        'fuse produced %d solids, valid=%s' % (len(r.Solids), r.isValid())
    return r.Solids[0]


def pocket_solid(z0, z1):
    """Pocket tool = the board outline plus clearance, as ONE polygon.

    Do NOT union this with the master's octagon.  That tool has walls exactly
    coincident with the pocket walls already in the master, and OCC then
    silently fails the cut.  A single polygon is also sufficient: the master
    keeps whatever the tool does not reach, so the resulting pocket is the
    union of the two outlines regardless."""
    return solid(prism(F_BOARD, z0, z1))


def outer_solid(z0, z1):
    """Outer envelope of the SNOUT only -- deliberately does NOT include the
    Ø70 body circle.  A profile containing that circle produces a wall exactly
    coincident with the master's own Ø70 cylinder, and OCC then returns an
    invalid solid from the fuse.  Everything inside r=35 is already provided by
    the master, so the snout only has to cover what sticks out past it."""
    return fuse_all([prism(F_BODY, z0, z1), prism(F_TIP, z0, z1)])


def outer_wire(z):
    """Outer boundary of the snout profile, as a wire at height z."""
    ws = outer_solid(z - 0.5, z + 0.5).slice(Vector(0, 0, 1), z)
    return max(ws, key=lambda w: w.Length)


# ================================================================ BottomHousing
def build_bottom():
    print('BottomHousing  <- Unterteil.iges')
    src = Part.Shape(); src.read(IGES + 'Unterteil.iges')
    main, boss = src.Solids[0], src.Solids[1]
    base = fuse_all([main, boss])

    radius, apt, adir, th, bbore = fit_tube_axis(boss)
    m_ax = -math.tan(th)
    c_ax = apt.z - m_ax * apt.x
    print('   tube bore  R=%.4f (dia %.4f)   angle %.4f deg   z = %.6f x %+.5f'
          % (radius, 2 * radius, math.degrees(th), m_ax, c_ax))
    print('   master bore exits at x=%.3f ; new socket face at x=%.1f' % (bbore.XMax, KEEL_X[1]))
    slide = (KEEL_X[1] - bbore.XMax) / math.cos(th)
    print('   >>> slide the socket %.1f mm along its own axis  ->  SHORTEN THE TUBE BY %.1f mm'
          % (slide, slide))

    axis_z = lambda x: m_ax * x + c_ax
    def on_axis(x):
        return Vector(x, 0.0, axis_z(x))

    doc = new_doc('BottomHousing')
    o_base = feat(doc, 'OriginalHousing', base, 'OriginalHousing (Unterteil.iges)')

    # ---- added: snout body.  Its underside is FLOOR_Z outboard of BLEND_X and
    #      ramps back up inboard of it, so it runs into the master's cone and
    #      the Arduino well ends up fully enclosed.
    wedge_pts = [(SNOUT_X0, FLOOR_Z + BLEND_SLOPE * (BLEND_X - SNOUT_X0)),
                 (BLEND_X, FLOOR_Z), (300.0, FLOOR_Z), (300.0, 0.0), (SNOUT_X0, 0.0)]
    pw = [Vector(x, 0, z) for x, z in ccw(wedge_pts)]
    wedge = (Part.Face(Part.makePolygon(pw + [pw[0]]))
             .extrude(Vector(0, 200.0, 0)).translated(Vector(0, -100.0, 0)))
    snout = solid(outer_solid(-30.0, 1.0).common(wedge))
    o_snout = feat(doc, 'SnoutBody', snout, 'SnoutBody')

    # ---- added: keel carrying the relocated socket.  Its underside hugs the
    #      bore outboard of the blind end and the smaller cable duct inboard.
    kb_bore = lambda x: axis_z(x) - radius - KEEL_GAP
    kb_duct = lambda x: axis_z(x) - DUCT_R - KEEL_GAP
    x_duct0 = (FLOOR_Z + DUCT_R + KEEL_GAP - c_ax) / m_ax
    prof = [(KEEL_X[0], FLOOR_Z + 1.0), (KEEL_X[1], FLOOR_Z + 1.0),
            (KEEL_X[1], kb_bore(KEEL_X[1])), (SOCKET_X, kb_bore(SOCKET_X)),
            (SOCKET_X, kb_duct(SOCKET_X)), (x_duct0, FLOOR_Z), (KEEL_X[0], FLOOR_Z)]
    pts = [Vector(x, 0, z) for x, z in prof]
    keel = solid(Part.Face(Part.makePolygon(pts + [pts[0]]))
                 .extrude(Vector(0, 2 * KEEL_Y, 0)).translated(Vector(0, -KEEL_Y, 0)))
    o_keel = feat(doc, 'SocketKeel', keel, 'SocketKeel')

    o_add = fuse(doc, 'Added', [o_base, o_snout, o_keel], 'Added (housing + snout + keel)')

    # ---- cut: PCB pocket
    o_pocket = feat(doc, 'PcbPocket', pocket_solid(PCB_BOT, 8.0), 'PcbPocket')
    c1 = cut(doc, 'CutPocket', o_add, o_pocket, 'after PcbPocket')

    # ---- cut: Arduino well (wider inboard so the 12 V cable can pass the Nano)
    w1 = box_obj(doc, 'WellMain', ARD_X[1] - ARD_X[0], 2 * ARD_Y, PCB_BOT - ARD_Z,
                 (ARD_X[0], -ARD_Y, ARD_Z), 'WellMain')
    w2 = box_obj(doc, 'WellCableEnd', ARD_WIDE_X - ARD_X[0], 2 * ARD_Y_WIDE, PCB_BOT - ARD_Z,
                 (ARD_X[0], -ARD_Y_WIDE, ARD_Z), 'WellCableEnd')
    o_well = fuse(doc, 'ArduinoWell', [w1, w2], 'ArduinoWell')
    c2 = cut(doc, 'CutWell', c1, o_well, 'after ArduinoWell')

    # ---- cut: arm tube socket, blind at SOCKET_X (that blind face is the stop)
    o_bore = cyl_obj(doc, 'ArmTubeSocket', radius, (130.0 - SOCKET_X) / math.cos(th),
                     tuple(on_axis(SOCKET_X)), adir, 'ArmTubeSocket')
    c3 = cut(doc, 'CutSocket', c2, o_bore, 'after ArmTubeSocket')

    # ---- cut: cable duct on to the old notch
    o_duct = cyl_obj(doc, 'CableDuct', DUCT_R, (SOCKET_X + 1.0 - DUCT_X0) / math.cos(th),
                     tuple(on_axis(DUCT_X0)), adir, 'CableDuct')
    c4 = cut(doc, 'CutDuct', c3, o_duct, 'after CableDuct')

    # ---- cut: cable riser, on the master's original notch centre
    o_riser = cyl_obj(doc, 'CableRiser', RISER_R, RISER_Z[1] - RISER_Z[0],
                      (RISER_XY[0], RISER_XY[1], RISER_Z[0]), Vector(0, 0, 1), 'CableRiser')
    c4 = cut(doc, 'CutRiser', c4, o_riser, 'after CableRiser')

    # ---- cut: extra cover screw in the tip block
    o_scr = cyl_obj(doc, 'TipScrewHole', SCREW_R, 14.0,
                    (SCREW_XY[0], SCREW_XY[1], -14.0), Vector(0, 0, 1), 'TipScrewHole')
    result = cut(doc, 'BottomHousing', c4, o_scr, 'BottomHousing (result)')

    doc.recompute()
    export(doc, result, 'bot.stl')
    doc.saveAs(FCDIR + 'BottomHousing.FCStd')
    write_gui_data(FCDIR + 'BottomHousing.FCStd', {result.Name})
    return radius, m_ax, c_ax, th


# ===================================================================== TopCover
def build_top():
    print('TopCover       <- Oberteil.iges')
    src = Part.Shape(); src.read(IGES + 'Oberteil.iges')
    base = fuse_all([src.Solids[0], src.Solids[1]])   # dome + locating rib

    doc = new_doc('TopCover')
    o_base = feat(doc, 'OriginalCover', base, 'OriginalCover (Oberteil.iges)')

    rib  = solid(prism(F_RIBOUT, RIB_Z, 0.0).common(half_x_ge(COVER_X0)))
    wall = solid(outer_solid(0.0, COVER_SHOULDER).common(half_x_ge(COVER_X0)))
    # true 45 deg chamfer up to the roof, lofted (not stepped)
    w_lo = outer_wire(COVER_SHOULDER)
    w_hi = Part.Face(outer_wire(COVER_ROOF)).makeOffset2D(
        -COVER_INSET, 0, False, False).OuterWire
    roof = solid(Part.makeLoft([w_lo, w_hi], True, True).common(half_x_ge(COVER_ROOF_X0)))

    o_rib  = feat(doc, 'CoverRib',  rib,  'CoverRib')
    o_wall = feat(doc, 'CoverWall', wall, 'CoverWall')
    o_roof = feat(doc, 'CoverRoof', roof, 'CoverRoof')
    o_add = fuse(doc, 'Added', [o_base, o_rib, o_wall, o_roof], 'Added (cover + snout)')

    o_ch = feat(doc, 'CoverChamber',
                solid(prism(F_CHAMBER, RIB_Z, COVER_CEIL).common(half_x_ge(COVER_X0))),
                'CoverChamber')
    c1 = cut(doc, 'CutChamber', o_add, o_ch, 'after CoverChamber')

    o_scr = cyl_obj(doc, 'TipScrewHole', SCREW_R, 12.0,
                    (SCREW_XY[0], SCREW_XY[1], -2.0), Vector(0, 0, 1), 'TipScrewHole')
    result = cut(doc, 'TopCover', c1, o_scr, 'TopCover (result)')

    doc.recompute()
    export(doc, result, 'top_1.stl')
    doc.saveAs(FCDIR + 'TopCover.FCStd')
    write_gui_data(FCDIR + 'TopCover.FCStd', {result.Name})



# ========================================================== BottomBearingHolder
def build_bearing_holder():
    """Stretch the carrier stem by STEM_EXTRA so the cup wheel drops clear of
    the relocated arm socket."""
    print('BottomBearingHolder  <- Unterteil-2.iges')
    src = Part.Shape(); src.read(IGES + 'Unterteil-2.iges')
    base = src.Solids[0]
    z0, z1 = base.BoundBox.ZMin, base.BoundBox.ZMax

    lower = solid(base.common(Part.makeBox(BIG, BIG, STEM_CUT_Z - z0 + 1.0,
                                           Vector(-BIG / 2, -BIG / 2, z0 - 1.0))))
    upper = solid(base.common(Part.makeBox(BIG, BIG, z1 - STEM_CUT_Z + 1.0,
                                           Vector(-BIG / 2, -BIG / 2, STEM_CUT_Z))))
    upper = upper.translated(Vector(0, 0, STEM_EXTRA))

    # the inserted length is the carrier's own section at the cut
    wires = base.slice(Vector(0, 0, 1), STEM_CUT_Z)
    wires.sort(key=lambda w: Part.Face(w).Area, reverse=True)
    bridge = solid(Part.Face(wires).extrude(Vector(0, 0, STEM_EXTRA)))

    out = fuse_all([lower, bridge, upper])
    print('   stem +%.1f mm at z=%.1f   length %.1f -> %.1f mm   bearing span ~%.0f -> ~%.0f mm'
          % (STEM_EXTRA, STEM_CUT_Z, z1 - z0, out.BoundBox.ZLength,
             30.0, 30.0 + STEM_EXTRA))

    doc = new_doc('BottomBearingHolder')
    o_orig = feat(doc, 'OriginalCarrier', base, 'OriginalCarrier (Unterteil-2.iges)')
    o_lo = feat(doc, 'FlangeEnd', lower, 'FlangeEnd (625 bearing, unmoved)')
    o_br = feat(doc, 'StemInsert', bridge, 'StemInsert (+%.1f mm)' % STEM_EXTRA)
    o_up = feat(doc, 'ShaftEnd', upper, 'ShaftEnd (695 bearing, dropped %.1f mm)' % STEM_EXTRA)
    result = fuse(doc, 'BottomBearingHolder', [o_lo, o_br, o_up],
                  'BottomBearingHolder (result)')
    doc.recompute()
    export(doc, result, 'bot_ball_bearing.stl')
    doc.saveAs(FCDIR + 'BottomBearingHolder.FCStd')
    write_gui_data(FCDIR + 'BottomBearingHolder.FCStd', {result.Name})


# =============================================== plain IGES -> FreeCAD, renamed
PLAIN = [
    ('Oberteil.iges',         'TopCap',              ['TopCap'], 2),
    ('Windfahne.iges',        'WindVane',            ['WindVane']),
    ('Magnethalter.iges',     'MagnetHolder',        ['MagnetHolder']),
    ('Fuss_regler.iges',      'MastBase',            ['MastBase']),
    ('Windex-Base.iges',      'WindexBase',          ['WindexBase']),
    ('Loeffel_mitte.iges',    'CupWheelHub',         ['CupWheelHub']),
    ('Loeffel_rund.iges',     'CupRound',            ['CupBowl', 'CupArmOuter',
                                                      'CupArmInner', 'CupCollar']),
    ('Loeffel_spitz v1.iges', 'CupPointed',          ['CupBowl', 'CupArmOuter',
                                                      'CupArmInner']),
]


def build_plain():
    for entry in PLAIN:
        src, name, labels = entry[0], entry[1], entry[2]
        only = entry[3] if len(entry) > 3 else None
        sh = Part.Shape(); sh.read(IGES + src)
        solids = sh.Solids if only is None else [sh.Solids[only]]
        doc = new_doc(name)
        for i, s in enumerate(solids):
            lbl = labels[i] if i < len(labels) else '%s_%d' % (name, i)
            feat(doc, '%s_%d' % (name, i) if len(solids) > 1 else name, s, lbl)
        doc.recompute()
        doc.saveAs(FCDIR + name + '.FCStd')
        write_gui_data(FCDIR + name + '.FCStd', {o.Name for o in doc.Objects})
        print('   %-22s <- %-24s %d solid(s)' % (name + '.FCStd', src, len(solids)))


def clean_backups():
    """FreeCAD drops a .FCBak beside every document it overwrites."""
    for f in os.listdir(FCDIR):
        if f.endswith('.FCBak'):
            os.remove(os.path.join(FCDIR, f))


def main():
    if not os.path.isdir(FCDIR):
        os.makedirs(FCDIR)
    build_bottom()
    build_top()
    build_bearing_holder()
    print('plain conversions')
    build_plain()
    clean_backups()
    print('done')


# freecadcmd execs this file with __name__ set to the module name, not
# '__main__', so run unconditionally unless imported as a library.
if __name__ != 'build_wind_sensor_lib':
    main()
