"""
Clean, printable replacement for the McMaster 69915K52 plastic submersible cord grip.

Run:  /Applications/FreeCAD.app/Contents/Resources/bin/freecadcmd build_cordgrip.py

Produces, in this directory:
    CordGripBody.FCStd / .stl   - male-threaded gland body, fuse this onto BackstayBase
    CordGripNut.FCStd  / .stl   - compression cap nut
    CordGripSeal.FCStd / .stl   - grommet (print in TPU, or use the rubber one out of 69915K52)
    build_cordgrip.log          - dimensions + validity report

Coordinates: Z is the cable axis, +Z out of the enclosure.  z = 0 is the mounting
face, i.e. the plane that sits on / inside the BackstayBase wall.  Everything is a
solid of revolution plus one swept helical thread -- no imported BREP, no assembly
of five interpenetrating solids.
"""
import math, os, sys, glob, struct, zipfile
import FreeCAD as App
import Part, Mesh, MeshPart
from FreeCAD import Vector

HERE = os.path.dirname(os.path.abspath(__file__)) or os.getcwd()
LOG  = open(os.path.join(HERE, "build_cordgrip.log"), "w")
def log(*a): LOG.write(" ".join(str(x) for x in a) + "\n")

# ---------------------------------------------------------------- parameters
# Seal / cable -- defaults match the rubber grommet inside 69915K52 so the
# printed body+nut can reuse that real rubber part if you have it.
SEAL_OD      = 11.2    # grommet outside diameter
SEAL_ID      = 6.0     # grommet bore = nominal cable diameter
SEAL_H       = 8.0     # grommet height
SEAL_EDGE_R  = 0.4     # round on the grommet bore edges
SEAL_FIT     = 0.2     # diametral clearance of the pocket over the grommet

CABLE_BORE   = 7.0     # straight duct through the body

# Thread -- 30 deg trapezoidal, coarse, chosen to print cleanly on FDM
TH_PITCH     = 3.0
TH_MAJOR_D   = 17.0
TH_MINOR_D   = 15.0
TH_W_ROOT    = 2.0     # axial width of the ridge at the minor diameter
TH_W_CREST   = 0.9     # axial width of the ridge at the major diameter
TH_CLEAR     = 0.25    # radial + flank clearance built into the nut

TH_Z0        = 2.5     # thread runs from here ...
TH_Z1        = 15.0    # ... to here
TH_CH        = 1.0     # 45 deg chamfer on both thread ends

# Body
FLANGE_D     = 20.0    # base pad that gets fused into BackstayBase
FLANGE_H     = 2.5
BODY_TOP_Z   = 16.0    # top face of the body
POCKET_MOUTH_CH = 0.5

# Nut
NUT_AF       = 24.0    # across flats
NUT_H        = 13.0
NUT_CH       = 1.2     # hex end chamfers
NUT_THREAD_H = 9.0     # depth of the female thread from the nut's bottom face
NUT_RELIEF_D = 15.4    # plain bore above the thread, clears the body's top collar
NUT_RELIEF_H = 1.5
NUT_EXIT_D   = 7.6     # cable exit hole
NUT_EXIT_CH  = 1.0

# ------------------------------------------------------------ derived layout
POCKET_D     = SEAL_OD + SEAL_FIT                 # 11.4
r_pocket     = POCKET_D / 2.0
r_bore       = CABLE_BORE / 2.0
# conical seat: 45 deg, grommet lands on it and gets wedged inward
SEAT_TOP_Z   = 9.6
SEAT_BOT_Z   = SEAT_TOP_Z - (r_pocket - r_bore)   # 45 deg
POCKET_DEPTH = BODY_TOP_Z - SEAT_TOP_Z
# where the grommet's outer bottom edge lands, and therefore how proud it sits
seal_bot_z   = SEAT_TOP_Z - (r_pocket - SEAL_OD / 2.0)
seal_top_z   = seal_bot_z + SEAL_H
SEAL_PROUD   = seal_top_z - BODY_TOP_Z

NUT_CEIL_Z   = NUT_THREAD_H + NUT_RELIEF_H        # nut-local z of the pressing face
nut_bot_first_contact = seal_top_z - NUT_CEIL_Z   # global z of nut bottom at touch
squeeze      = SEAL_PROUD                          # travel until nut ceiling meets body top

r_root       = TH_MINOR_D / 2.0
r_crest      = TH_MAJOR_D / 2.0

# ------------------------------------------------------------------ helpers
def revolve(pts):
    """Solid of revolution about Z from a closed (r, z) profile."""
    v = [Vector(r, 0, z) for r, z in pts]
    w = Part.makePolygon(v + [v[0]])
    return Part.Face(w).revolve(Vector(0, 0, 0), Vector(0, 0, 1), 360)

def thread_ridge(pitch, r_in_ref, r_out, w_in_ref, w_out, z_from, z_to, over=0.4):
    """Helical trapezoidal ridge swept from z_from to z_to.

    The profile is extrapolated `over` mm inboard of r_in_ref so it overlaps the
    core cylinder instead of sharing a face with it -- overlapping fuses are far
    more robust than coincident ones.
    """
    h = z_to - z_from
    helix = Part.makeHelix(pitch, h, r_in_ref)
    helix.translate(Vector(0, 0, z_from))
    slope = (w_out - w_in_ref) / (r_out - r_in_ref)
    r_in = r_in_ref - over
    w_in = w_in_ref + slope * (r_in - r_in_ref)
    pts = [Vector(r_in,  0, -w_in / 2.0),
           Vector(r_out, 0, -w_out / 2.0),
           Vector(r_out, 0,  w_out / 2.0),
           Vector(r_in,  0,  w_in / 2.0)]
    prof = Part.makePolygon(pts + [pts[0]])
    prof.translate(Vector(0, 0, z_from))
    wire = helix if isinstance(helix, Part.Wire) else Part.Wire(helix.Edges)
    return wire.makePipeShell([prof], True, True)

def ext_chamfer(r_root, z_face, ch, reach=4.0, beyond=6.0):
    """Ring tool that bevels the top end of an EXTERNAL thread back to r_root at
    z_face, and clears everything outside r_root above it.

    A swept ridge overshoots its nominal end by half a profile width, so the tool
    has to keep cutting past z_face -- otherwise that overshoot pokes out through
    whatever sits above the thread.

    Deliberately NOT an intersection against a cylinder of the crest radius: that
    boolean is unreliable on a multi-turn swept solid (OCC quietly returns either
    the whole envelope or nothing).  A cone cut touches far less of the sweep.
    """
    return revolve([(r_root, z_face),
                    (r_root + reach, z_face - reach * ch),
                    (r_root + reach, z_face + beyond),
                    (r_root, z_face + beyond)])


def int_chamfer(r_minor, r_major, z_face, lead, below=0.5):
    """Ring tool that bevels the mouth of an INTERNAL thread: at z_face the hole is
    open all the way to r_major, tapering back to full crest (r_minor) `lead` in."""
    return revolve([(r_minor, z_face - below),
                    (r_major, z_face - below),
                    (r_major, z_face + lead),
                    (r_minor, z_face)])

def refine(shape):
    """removeSplitter() tidies coplanar faces, but on a swept helical thread it can
    merge a band into a closed periodic face and produce an unorientable shell -- and
    it leaves the shape it was called on corrupted even when its result is thrown
    away.  So always call it on a copy, and keep the result only if it is still a
    single valid solid of the same volume."""
    try:
        r = shape.copy().removeSplitter()
    except Exception:
        return shape
    good = (r.isValid() and len(r.Solids) == 1 and len(r.Shells) == 1
            and abs(r.Volume - shape.Volume) < 1e-6 * max(1.0, shape.Volume))
    return r if good else shape


def check(name, shape):
    ok = True
    n_sol, n_shl = len(shape.Solids), len(shape.Shells)
    log("  %-12s valid=%s solids=%d shells=%d faces=%d vol=%.2f"
        % (name, shape.isValid(), n_sol, n_shl, len(shape.Faces), shape.Volume))
    if not shape.isValid() or n_sol != 1 or n_shl != 1:
        ok = False
    m = MeshPart.meshFromShape(Shape=shape, LinearDeflection=0.03,
                               AngularDeflection=0.12, Relative=False)
    m.harmonizeNormals()
    log("     mesh: pts=%d facets=%d solid=%s selfint=%s nonmanifold=%s "
        "invalidpts=%s components=%d"
        % (m.CountPoints, m.CountFacets, m.isSolid(), m.hasSelfIntersections(),
           m.hasNonManifolds(), m.hasInvalidPoints(), m.countComponents()))
    if not (m.isSolid() and not m.hasSelfIntersections()
            and not m.hasNonManifolds() and m.countComponents() == 1):
        ok = False
    return ok, m

# ------------------------------------------------------------------- gui xml
# freecadcmd has no GUI layer, so doc.saveAs() writes a .FCStd containing only
# Document.xml + the BREP.  Opened in the FreeCAD GUI such a file has no
# ViewProvider for its objects: no colour, no visibility, nothing in the tree.
# So synthesise GuiDocument.xml (plus the colour/material blobs it references)
# and inject them into the archive after saving.

def _le_rgba(rgb):
    """FreeCAD stores colours as a little-endian uint32 of 0xRRGGBBAA."""
    r, g, b = rgb
    return struct.pack("<I", (r << 24) | (g << 16) | (b << 8) | 0xFF)

def _color_list(rgb):
    return struct.pack("<I", 1) + _le_rgba(rgb)

def _material_list(rgb):
    return (struct.pack("<I", 1)
            + _le_rgba((0x33, 0x33, 0x33))     # ambient
            + _le_rgba(rgb)                    # diffuse
            + _le_rgba((0x88, 0x88, 0x88))     # specular
            + _le_rgba((0x00, 0x00, 0x00))     # emissive
            + struct.pack("<f", 0.9)           # shininess
            + struct.pack("<f", 0.0)           # transparency
            + struct.pack("<III", 0, 0, 0))    # image, imagePath, uuid (all empty)

_VP = """        <ViewProvider name="{name}" expanded="0" Extensions="True">
            <Extensions Count="1">
                <Extension type="Gui::ViewProviderFaceTexture" name="ViewProviderFaceTexture">
                </Extension>
            </Extensions>
            <Properties Count="23" TransientCount="0">
                <Property name="AngularDeflection" type="App::PropertyAngle" status="1">
                    <Float value="28.5000000000000000"/>
                </Property>
                <Property name="BoundingBox" type="App::PropertyBool" status="1">
                    <Bool value="false"/>
                </Property>
                <Property name="Deviation" type="App::PropertyFloatConstraint" status="1">
                    <Float value="0.2000000000000000"/>
                </Property>
                <Property name="DisplayMode" type="App::PropertyEnumeration" status="1">
                    <Integer value="0"/>
                </Property>
                <Property name="DrawStyle" type="App::PropertyEnumeration" status="1">
                    <Integer value="0"/>
                </Property>
                <Property name="Lighting" type="App::PropertyEnumeration" status="1">
                    <Integer value="1"/>
                </Property>
                <Property name="LineColor" type="App::PropertyColor" status="1">
                    <PropertyColor value="421075455"/>
                </Property>
                <Property name="LineColorArray" type="App::PropertyColorList" status="1">
                    <ColorList file="LineColorArray"/>
                </Property>
                <Property name="LineMaterial" type="App::PropertyMaterial" status="1">
                    <PropertyMaterial ambientColor="858993663" diffuseColor="421075455" specularColor="255" emissiveColor="255" shininess="1.0000000000000000" transparency="0.0000000000000000" image="" imagePath="" uuid=""/>
                </Property>
                <Property name="LineWidth" type="App::PropertyFloatConstraint" status="1">
                    <Float value="2.0000000000000000"/>
                </Property>
                <Property name="OnTopWhenSelected" type="App::PropertyEnumeration" status="1">
                    <Integer value="0"/>
                </Property>
                <Property name="PointColor" type="App::PropertyColor" status="1">
                    <PropertyColor value="421075455"/>
                </Property>
                <Property name="PointColorArray" type="App::PropertyColorList" status="1">
                    <ColorList file="PointColorArray"/>
                </Property>
                <Property name="PointMaterial" type="App::PropertyMaterial" status="1">
                    <PropertyMaterial ambientColor="858993663" diffuseColor="421075455" specularColor="255" emissiveColor="255" shininess="1.0000000000000000" transparency="0.0000000000000000" image="" imagePath="" uuid=""/>
                </Property>
                <Property name="PointSize" type="App::PropertyFloatConstraint" status="1">
                    <Float value="2.0000000000000000"/>
                </Property>
                <Property name="Selectable" type="App::PropertyBool" status="1">
                    <Bool value="true"/>
                </Property>
                <Property name="SelectionStyle" type="App::PropertyEnumeration" status="1">
                    <Integer value="0"/>
                </Property>
                <Property name="ShapeAppearance" type="App::PropertyMaterialList" status="1">
                    <MaterialList file="ShapeAppearance" version="3"/>
                </Property>
                <Property name="ShowInTree" type="App::PropertyBool" status="1">
                    <Bool value="true"/>
                </Property>
                <Property name="ShowPlacement" type="App::PropertyBool" status="1">
                    <Bool value="false"/>
                </Property>
                <Property name="TransformOrigin" type="App::PropertyPlacement" status="67108865">
                    <PropertyPlacement Px="0.0000000000000000" Py="0.0000000000000000" Pz="0.0000000000000000" Q0="0.0000000000000000" Q1="0.0000000000000000" Q2="0.0000000000000000" Q3="1.0000000000000000" A="0.0000000000000000" Ox="0.0000000000000000" Oy="0.0000000000000000" Oz="1.0000000000000000"/>
                </Property>
                <Property name="Transparency" type="App::PropertyPercent" status="1">
                    <Integer value="0"/>
                </Property>
                <Property name="Visibility" type="App::PropertyBool" status="1">
                    <Bool value="true"/>
                </Property>
            </Properties>
        </ViewProvider>
"""

def _camera(shape):
    """Axonometric view framing the part, so the file opens looking at something."""
    bb = shape.BoundBox
    q = (0.42438447, 0.17592973, 0.33861966, 0.82113868)   # FreeCAD's axonometric
    d = App.Rotation(*q).multVec(Vector(0, 0, 1))
    size = max(bb.XLength, bb.YLength, bb.ZLength)
    focal = 5.0 * size
    c = Vector(bb.Center.x, bb.Center.y, bb.Center.z) + d * focal
    cam = ("OrthographicCamera {\n  viewportMapping ADJUST_CAMERA\n"
           "  position %.6f %.6f %.6f\n"
           "  orientation %.8f %.8f %.8f  %.8f\n"
           "  nearDistance %.6f\n  farDistance %.6f\n  aspectRatio 1\n"
           "  focalDistance %.6f\n  height %.6f\n\n}\n"
           % (c.x, c.y, c.z, q[0], q[1], q[2], q[3],
              focal - size, focal + size, focal, size * 1.6))
    return (cam.replace("&", "&amp;").replace('"', "&quot;")
               .replace("<", "&lt;").replace(">", "&gt;")
               .replace("\n", "&#10;"))

def gui_document(name, shape):
    return ("<?xml version='1.0' encoding='utf-8'?>\n"
            "<!--\n FreeCAD Document, see https://www.freecad.org for more information...\n-->\n"
            '<Document SchemaVersion="1" HasExpansion="1">\n'
            "    <Expand />\n"
            '    <ViewProviderData Count="1">\n'
            + _VP.format(name=name) +
            "    </ViewProviderData>\n"
            '    <Camera settings="' + _camera(shape) + '"/>\n'
            "</Document>\n")

def inject_gui(path, name, shape, rgb):
    with zipfile.ZipFile(path, "a", zipfile.ZIP_DEFLATED) as z:
        existing = set(z.namelist())
        for fn, data in (("GuiDocument.xml", gui_document(name, shape).encode("utf-8")),
                         ("LineColorArray",  _color_list((0x19, 0x19, 0x19))),
                         ("PointColorArray", _color_list((0x19, 0x19, 0x19))),
                         ("ShapeAppearance", _material_list(rgb))):
            if fn not in existing:
                z.writestr(fn, data)


def emit(name, shape, mesh, label, rgb=(0x72, 0x79, 0x80)):
    doc = App.newDocument(name)
    o = doc.addObject("Part::Feature", name)
    o.Shape = shape
    o.Label = label
    doc.recompute()
    fcstd = os.path.join(HERE, name + ".FCStd")
    doc.saveAs(fcstd)
    App.closeDocument(doc.Name)
    # FreeCAD drops <name>.<timestamp>.FCBak next to the file; keep the tree tidy
    for bak in glob.glob(os.path.join(HERE, name + ".*.FCBak")):
        os.remove(bak)
    inject_gui(fcstd, name, shape, rgb)
    mesh.write(os.path.join(HERE, name + ".stl"))

# --------------------------------------------------------------------- body
outer = revolve([
    (0, 0), (FLANGE_D / 2.0, 0), (FLANGE_D / 2.0, FLANGE_H - 0.8),
    (FLANGE_D / 2.0 - 0.8, FLANGE_H),          # small chamfer off the flange
    (r_root, FLANGE_H),
    (r_root, BODY_TOP_Z),
    (0, BODY_TOP_Z),
])
ridge = thread_ridge(TH_PITCH, r_root, r_crest, TH_W_ROOT, TH_W_CREST, TH_Z0, TH_Z1)
body = refine(outer.fuse(ridge))
# lead-in bevel at the top; the bottom end of the thread just runs into the
# flange, which is wider than the crest, so it needs no bevel of its own.
body = refine(body.cut(ext_chamfer(r_root, TH_Z1, TH_CH)))

body_bore = revolve([
    (0, -1), (r_bore, -1),
    (r_bore, SEAT_BOT_Z),
    (r_pocket, SEAT_TOP_Z),
    (r_pocket, BODY_TOP_Z - POCKET_MOUTH_CH),
    (r_pocket + POCKET_MOUTH_CH, BODY_TOP_Z),
    (r_pocket + POCKET_MOUTH_CH, BODY_TOP_Z + 1),
    (0, BODY_TOP_Z + 1),
])
body = refine(body.cut(body_bore))

# ---------------------------------------------------------------------- nut
R_corner = NUT_AF / math.sqrt(3.0)
hexpts = [Vector(R_corner * math.cos(math.radians(60 * i)),
                 R_corner * math.sin(math.radians(60 * i)), 0) for i in range(6)]
hexwire = Part.makePolygon(hexpts + [hexpts[0]])
nut = Part.Face(hexwire).extrude(Vector(0, 0, NUT_H))
barrel = revolve([(0, 0), (R_corner - NUT_CH, 0), (R_corner, NUT_CH),
                  (R_corner, NUT_H - NUT_CH), (R_corner - NUT_CH, NUT_H), (0, NUT_H)])
nut = nut.common(barrel)

# female thread = the male shank, dilated by TH_CLEAR, cut away
c = TH_CLEAR
tool_r_in, tool_r_out = r_root - c, r_crest + c
tool_w_in, tool_w_out = TH_W_ROOT + 2 * c, TH_W_CREST + 2 * c
TOOL_Z0 = -3.0                      # tool starts below the nut so the thread runs
tool = revolve([(0, TOOL_Z0), (tool_r_in, TOOL_Z0),                # out through the
                (tool_r_in, NUT_THREAD_H), (0, NUT_THREAD_H)])     # bottom face
tool_ridge = thread_ridge(TH_PITCH, tool_r_in, tool_r_out, tool_w_in, tool_w_out,
                          TOOL_Z0, NUT_THREAD_H)
tool = refine(tool.fuse(tool_ridge))
# the sweep overshoots z = NUT_THREAD_H by half a profile width; shave it off with
# a planar cut (safe) rather than an intersection against a coaxial cylinder (not)
tool = refine(tool.cut(revolve([(0, NUT_THREAD_H), (tool_r_out + 4, NUT_THREAD_H),
                                (tool_r_out + 4, NUT_THREAD_H + 8),
                                (0, NUT_THREAD_H + 8)])))
nut = refine(nut.cut(tool))
nut = refine(nut.cut(int_chamfer(tool_r_in, tool_r_out, 0.0, TH_CH)))   # entry bevel

r_relief, r_exit = NUT_RELIEF_D / 2.0, NUT_EXIT_D / 2.0
nut_bore = revolve([
    (0, NUT_THREAD_H), (r_relief, NUT_THREAD_H),
    (r_relief, NUT_CEIL_Z),
    (r_exit, NUT_CEIL_Z),
    (r_exit, NUT_H - NUT_EXIT_CH),
    (r_exit + NUT_EXIT_CH, NUT_H),
    (0, NUT_H),
])
nut = refine(nut.cut(nut_bore))

# --------------------------------------------------------------------- seal
rs_o, rs_i, e = SEAL_OD / 2.0, SEAL_ID / 2.0, SEAL_EDGE_R
seal = revolve([
    (rs_i, 0), (rs_o, 0), (rs_o, SEAL_H), (rs_i, SEAL_H),
])
# round the two bore edges the way the original grommet does
seal = seal.cut(revolve([(rs_i, -0.01), (rs_i + e, -0.01), (rs_i + e, e), (rs_i, e)])
                .cut(Part.makeTorus(rs_i + e, e, Vector(0, 0, e))))
seal = seal.cut(revolve([(rs_i, SEAL_H + 0.01), (rs_i + e, SEAL_H + 0.01),
                         (rs_i + e, SEAL_H - e), (rs_i, SEAL_H - e)])
                .cut(Part.makeTorus(rs_i + e, e, Vector(0, 0, SEAL_H - e))))
seal.translate(Vector(0, 0, seal_bot_z))
seal = refine(seal)

# ------------------------------------------------------------------ report
log("CordGrip -- clean replacement for McMaster 69915K52")
log("")
log("thread          M%.1f x %.1f trapezoidal, minor %.1f, %.1f turns of engagement"
    % (TH_MAJOR_D, TH_PITCH, TH_MINOR_D, (TH_Z1 - TH_Z0) / TH_PITCH))
log("flank angle     %.1f deg from radial (%.1f deg overhang from horizontal)"
    % (math.degrees(math.atan(((TH_W_ROOT - TH_W_CREST) / 2.0) / (r_crest - r_root))),
       math.degrees(math.atan((r_crest - r_root) / ((TH_W_ROOT - TH_W_CREST) / 2.0)))))
log("nut crest flat  %.2f mm (pitch %.1f - dilated tool width %.2f)"
    % (TH_PITCH - tool_w_in, TH_PITCH, tool_w_in))
log("body            flange D%.1f x %.1f, top face z=%.1f, bore D%.1f"
    % (FLANGE_D, FLANGE_H, BODY_TOP_Z, CABLE_BORE))
log("pocket          D%.1f x %.2f deep, 45 deg seat D%.1f->D%.1f at z=%.2f..%.2f"
    % (POCKET_D, POCKET_DEPTH, POCKET_D, CABLE_BORE, SEAT_BOT_Z, SEAT_TOP_Z))
log("pocket wall     %.2f mm (thread root r%.2f - pocket r%.2f)"
    % (r_root - r_pocket, r_root, r_pocket))
log("seal            D%.1f x %.1f, bore D%.1f; sits z=%.2f..%.2f, %.2f mm proud"
    % (SEAL_OD, SEAL_H, SEAL_ID, seal_bot_z, seal_top_z, SEAL_PROUD))
log("nut             hex AF%.1f (AC %.2f) x %.1f, exit D%.1f, wall %.2f"
    % (NUT_AF, 2 * R_corner, NUT_H, NUT_EXIT_D, NUT_AF / 2.0 - tool_r_out))
log("nut first touch bottom face at z=%.2f; bottoms on body top after %.2f mm"
    % (nut_bot_first_contact, squeeze))
log("compression     %.1f%% of seal height" % (100.0 * squeeze / SEAL_H))
log("assembly height %.2f mm above the mounting face at first touch"
    % (nut_bot_first_contact + NUT_H))
log("thread engagement at full squeeze: %.2f mm"
    % (min(TH_Z1, nut_bot_first_contact - squeeze + NUT_THREAD_H)
       - (nut_bot_first_contact - squeeze)))
log("")
log("validity:")

ok_b, mb = check("body", body)
ok_n, mn = check("nut",  nut)
ok_s, ms = check("seal", seal)

# --------------------------------------------- thread presence + fit checks
# Booleans are NOT trustworthy for measuring these shapes: Shape.common() against
# a coaxial cylinder reports 0 mm3 inside r=8.499 but 1180 mm3 inside r=8.501 on
# the same solid, and distToShape cannot tell contact from interference.  So
# measure thread presence off real section geometry, and prove the clearance from
# the generator profiles, which is exact 2D arithmetic.

def section(shape, deg=7.0):
    """Half-section wires on a plane through the axis (7 deg avoids slicing the
    hex exactly through its corners, which is degenerate and returns nothing)."""
    th = math.radians(deg)
    return shape.slice(Vector(-math.sin(th), math.cos(th), 0), 0)

def radii(shape, z_lo, z_hi, deg=7.0):
    out = []
    for w in section(shape, deg):
        for e in w.Edges:
            rr = [(math.hypot(p.x, p.y), p.z) for p in e.discretize(80)]
            rr = [(r, z) for r, z in rr if z_lo <= z <= z_hi and r > 0.1]
            if rr:
                out.append((min(r for r, _ in rr), max(r for r, _ in rr),
                            min(z for _, z in rr), max(z for _, z in rr)))
    return out

log("")
log("thread presence, measured off the y-section of the actual solids:")
b = radii(body, TH_Z0 + TH_PITCH, TH_Z1 - TH_PITCH)
b_max = max(hi for _, hi, _, _ in b)
log("  body: max radius over the thread band = %.3f (root %.3f, crest %.3f) -> %s"
    % (b_max, r_root, r_crest,
       "THREADED" if b_max > r_crest - 0.05 else "*** NO THREAD ***"))
n = radii(nut, 1.0, NUT_THREAD_H - 1.0)
n_min = min(lo for lo, _, _, _ in n)
crest_edges = sorted((zl + zh) / 2.0 for lo, _, zl, zh in n if lo < tool_r_in + 0.02)
log("  nut : min radius over the thread band = %.3f (crest %.3f, major %.3f) -> %s"
    % (n_min, tool_r_in, tool_r_out,
       "THREADED" if n_min < tool_r_in + 0.05 else "*** NO THREAD ***"))
log("  nut : section edges reaching the crest radius at z = %s"
    % ", ".join("%.2f" % z for z in crest_edges))
log("  nut : %.1f turns of female thread over %.1f mm at %.1f mm pitch"
    % (NUT_THREAD_H / TH_PITCH, NUT_THREAD_H, TH_PITCH))
ok_t = (b_max > r_crest - 0.05) and (n_min < tool_r_in + 0.05)

# Both threads are swept along helices of identical pitch, so the fit is decided
# entirely by the 2D profiles: the female trapezoid must enclose the male one at
# every radius they share.
def half_width(r, r_lo, r_hi, w_lo, w_hi):
    return (w_lo + (w_hi - w_lo) * (r - r_lo) / (r_hi - r_lo)) / 2.0

log("")
log("thread fit (female profile vs male profile, per flank):")
worst = 1e9
for i in range(41):
    r = r_root + (r_crest - r_root) * i / 40.0
    male = half_width(r, r_root, r_crest, TH_W_ROOT, TH_W_CREST)
    fem = half_width(r, tool_r_in, tool_r_out, tool_w_in, tool_w_out)
    worst = min(worst, fem - male)
    if i % 20 == 0:
        log("  r=%.3f  male half-width %.3f  female half-width %.3f  clearance %.3f"
            % (r, male, fem, fem - male))
log("  minimum flank clearance = %.3f mm; radial clearance = %.3f mm at root and crest"
    % (worst, TH_CLEAR))
ok_f = worst > 0.05 and TH_CLEAR > 0.05

log("")
log("assembly:")
log("  nut first touches the seal with its bottom face at z=%.2f, %.2f turns engaged"
    % (nut_bot_first_contact, (TH_Z1 - nut_bot_first_contact) / TH_PITCH))
log("  fully tightened: bottom face z=%.2f, %.2f turns engaged, %.2f mm of squeeze"
    % (nut_bot_first_contact - squeeze,
       min(NUT_THREAD_H, TH_Z1 - (nut_bot_first_contact - squeeze)) / TH_PITCH, squeeze))

emit("CordGripBody", body, mb, "Cord grip body (fuse to BackstayBase)", (0x4A, 0x90, 0xD9))
emit("CordGripNut",  nut,  mn, "Cord grip cap nut", (0x5F, 0xBF, 0x7F))
emit("CordGripSeal", seal, ms, "Cord grip seal / grommet (TPU)", (0xC0, 0x39, 0x2B))

log("")
log("ALL CLEAN" if (ok_b and ok_n and ok_s and ok_t and ok_f)
    else "*** PROBLEM ***")
LOG.close()
