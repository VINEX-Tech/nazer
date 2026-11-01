"""NAZER prototype enclosure (PETG, Bambu A1): parametric, all dimensions in mm.

usage:  python nazer_enclosure.py <logo_full.png> <out_dir>
needs:  manifold3d numpy trimesh scikit-image pillow

Frame: origin at the PCB's front-left corner projected onto the base floor.
  x along the 102.1 mm edge (USB-C + slide switch are on the front edge, y = 0)
  y toward the back (antenna bay behind the PCB), z up from the base floor.
Measured 2026-10-16 with calipers on the prototype board.
"""
import sys
from pathlib import Path

import numpy as np
import trimesh
from manifold3d import CrossSection, FillRule, JoinType, Manifold, OpType
from PIL import Image
from skimage import measure

LOGO_PNG, OUT = Path(sys.argv[1]), Path(sys.argv[2])
OUT.mkdir(parents=True, exist_ok=True)

# ---- board (measured) ----------------------------------------------------------
PCB_L, PCB_W, PCB_T = 102.10, 79.88, 1.55
PIN_GAP = 6.5            # floor → PCB bottom: pins 4.71 + wires 1.3 + margin
CELL_TOP = 21.80         # PCB bottom → top of the 18650 (tallest part)
USB_X0, USB_W = 9.79, 8.94          # socket starts 9.79 from the left corner
USB_Z0, USB_H = 3.61, 3.26          # above the PCB top
SW_X0, SW_X1 = 28.83, 39.64         # slide knob travel along the front edge
SW_TOP = 10.79 - PCB_T              # knob top above the PCB top
SW_SPACE = 5.20                     # knob protrudes this far past the PCB edge

# ---- enclosure -------------------------------------------------------------------
WALL, FLOOR, TOP = 2.5, 3.0, 2.5
GAP_SIDE = 1.6           # edge wires 1.3 + 0.3
GAP_FRONT = SW_SPACE + 0.4
GAP_RIGHT = 2.5          # battery holder end
BAY = 12.0               # antenna bay behind the PCB
HEAD = 1.5               # air above the cell
CORNER_R = 4.0

X0, X1 = -GAP_SIDE, PCB_L + GAP_RIGHT
Y0, Y1 = -GAP_FRONT, PCB_W + GAP_SIDE + BAY
ZPCB = PIN_GAP
ZTOP_IN = PIN_GAP + CELL_TOP + HEAD
OX0, OX1, OY0, OY1 = X0 - WALL, X1 + WALL, Y0 - WALL, Y1 + WALL

# antenna: RP-SMA bulkhead jack + 50 mm rubber whip, ~10 mm thick
ANT_Y = PCB_W + GAP_SIDE + BAY / 2
ANT_Z = 9.0
ANT_X0, ANT_X1 = 12.0, 86.0         # jack body … whip tip
BRACKET_X = 9.0                     # bracket plate (jack nut clamps on it)
SMA_HOLE = 6.6                      # 1/4"-36 thread measured 6.08

SCREW_PILOT, SCREW_CLR, SCREW_HEAD = 2.6, 3.4, 6.2   # M3 self-tapping into PETG
BOSS_R = 3.3
BOSSES = [(1.8, Y0 + 1.4), (X1 - 1.9, Y0 + 1.4), (1.8, Y1 - 2.6), (X1 - 1.9, Y1 - 2.6)]


def box(x0, x1, y0, y1, z0, z1):
    return Manifold.cube([x1 - x0, y1 - y0, z1 - z0]).translate([x0, y0, z0])


def rounded_rect(x0, x1, y0, y1, r):
    sq = CrossSection.square([x1 - x0 - 2 * r, y1 - y0 - 2 * r]).translate([x0 + r, y0 + r])
    return sq.offset(r, JoinType.Round, circular_segments=48)


def prism(cs, z0, z1):
    return Manifold.extrude(cs, z1 - z0).translate([0, 0, z0])


def cyl(x, y, z0, z1, r, seg=48):
    return Manifold.cylinder(z1 - z0, r, r, seg).translate([x, y, z0])


def cyl_x(x0, x1, y, z, r, seg=48):
    return Manifold.cylinder(x1 - x0, r, r, seg).rotate([0, 90, 0]).translate([x0, y, z])


def cyl_y(y0, y1, x, z, r, seg=48):
    return Manifold.cylinder(y1 - y0, r, r, seg).rotate([-90, 0, 0]).translate([x, y0, z])


def logo_section(width):
    """NAZER wordmark (white-on-transparent PNG) → CrossSection `width` mm wide."""
    a = np.asarray(Image.open(LOGO_PNG).convert("RGBA"))[:, :, 3].astype(float) / 255
    step = max(1, a.shape[1] // 1400)
    a = np.pad(a[::step, ::step], 2)
    contours = measure.find_contours(a, 0.5)
    s = width / a.shape[1]
    polys = []
    for c in contours:
        if len(c) < 8:
            continue
        c = measure.approximate_polygon(c, tolerance=0.6)
        polys.append([(float(p[1]) * s, float(-p[0]) * s) for p in c])
    cs = CrossSection(polys, FillRule.EvenOdd)
    b = cs.bounds()                      # (xmin, ymin, xmax, ymax)
    return cs.translate([-(b[0] + b[2]) / 2, -(b[1] + b[3]) / 2])


# =================================================================== lid (deep shell)
outer = rounded_rect(OX0, OX1, OY0, OY1, CORNER_R)
inner = rounded_rect(X0, X1, Y0, Y1, max(0.5, CORNER_R - WALL))
lid = prism(outer, 0, ZTOP_IN + TOP) - prism(inner, 0, ZTOP_IN)

for bx, by in BOSSES:                    # screw columns, merged into the walls
    lid += cyl(bx, by, 0, ZTOP_IN, BOSS_R)
    lid -= cyl(bx, by, -1, 14, SCREW_PILOT / 2)

# hold-down fingers on the two left PCB corners (stick 1 mm foam on the tips)
for fy0, fy1 in ((0.4, 4.4), (PCB_W - 4.4, PCB_W - 0.4)):
    lid += box(0.4, 4.4, fy0, fy1, ZPCB + PCB_T + 0.2, ZTOP_IN)
# pressure pad over the 18650 (keeps the cell in its holder under shock)
lid += box(80.0, PCB_L - 1.0, 8.0, PCB_W - 8.0, ZPCB + CELL_TOP + 0.5, ZTOP_IN)

# front wall openings
usb_cx, usb_cz = USB_X0 + USB_W / 2, ZPCB + PCB_T + USB_Z0 + USB_H / 2
usb_hole = rounded_rect(usb_cx - 6.5, usb_cx + 6.5, usb_cz - 4.0, usb_cz + 4.0, 2.0)
lid -= prism(usb_hole, 0, 1).scale([1, 1, 30]).rotate([90, 0, 0]).translate([0, Y0 + 15, 0])
sw_z1 = ZPCB + PCB_T + SW_TOP + 0.8
slot = rounded_rect(SW_X0 - 0.6, SW_X1 + 0.6, sw_z1 - 7.0, sw_z1, 1.5)
lid -= prism(slot, 0, 1).scale([1, 1, 30]).rotate([90, 0, 0]).translate([0, Y0 + 15, 0])

# Logo: PLA inlay flush with the PETG top (0.8 mm, 4 layers). PLA does not bond to PETG, so each
# letter has a 0.4 mm wider flange under the surface that the PETG covers from
# behind: the inlay is trapped mechanically. Mark and wordmark are separate
# bodies so they can take two colours (Fire Engine Red + Orange).
LOGO_W, LOGO_D, FLANGE_D, FLANGE_GROW = 84.0, 0.8, 0.4, 0.4
MIN_FEATURE = 0.4                                   # one 0.4 mm nozzle line
raw = logo_section(LOGO_W).translate([(OX0 + OX1) / 2, (OY0 + OY1) / 2 + 4])
# close (pixel pinches) then open (drop slivers under one nozzle line, e.g. the arc's sharp tip)
logo_cs = (raw.offset(0.03, JoinType.Round).offset(-0.03, JoinType.Round)
           .offset(-MIN_FEATURE / 2, JoinType.Round).offset(MIN_FEATURE / 2, JoinType.Round).simplify(0.02))
# flange, with gaps narrower than ~0.8 mm filled so no thin PETG rib is left between letters
flange_cs = (logo_cs.offset(FLANGE_GROW + 0.4, JoinType.Round)
             .offset(-0.4, JoinType.Round).simplify(0.02))
zt = ZTOP_IN + TOP

# Colour split by shape, not by a line: pieces left of the first surface gap are the mark (red),
# the rest the wordmark (orange). The flanges overlap across that gap, so red keeps its flange and
# orange's flange gives way, with any leftover thinner than one nozzle line removed.
def opened(cs, r=MIN_FEATURE / 2):
    return cs.offset(-r, JoinType.Round).offset(r, JoinType.Round)


pieces = sorted(logo_cs.decompose(), key=lambda p: p.bounds()[0])
reach, cut = pieces[0].bounds()[2], len(pieces)
for i, p in enumerate(pieces[1:], 1):
    if p.bounds()[0] > reach:
        cut = i
        break
    reach = max(reach, p.bounds()[2])
mark_cs = opened(opened(CrossSection.batch_boolean(pieces[:cut], OpType.Add), -0.25))   # join gaps < 0.5, round tips
text_cs = opened(opened(CrossSection.batch_boolean(pieces[cut:], OpType.Add), -0.25))
logo_cs = mark_cs + text_cs
flange_cs = opened(logo_cs.offset(FLANGE_GROW + 0.4, JoinType.Round).offset(-0.4, JoinType.Round)).simplify(0.02)
mark_fl = (flange_cs ^ mark_cs.offset(1.0, JoinType.Round)) - text_cs.offset(0.3, JoinType.Round)
text_fl = flange_cs - mark_fl                                    # orange owns the rest


def layered(surface, flange):
    return prism(surface, zt - LOGO_D, zt) + prism(flange, zt - LOGO_D - FLANGE_D, zt - LOGO_D + 0.01)


logo_mark = layered(mark_cs, mark_fl)
logo_text = layered(text_cs, text_fl)
# recess = full logo + full flange (= red ∪ orange exactly), cut without the split line: no pinches
lid -= layered(logo_cs, flange_cs) + prism(logo_cs, zt - 0.01, zt + 1)
logo_inlay = logo_mark + logo_text

# ====================================================================== base (plate)
base = prism(outer, -FLOOR, 0)
lip_o = rounded_rect(X0 + 0.25, X1 - 0.25, Y0 + 0.25, Y1 - 0.25, 1.0)
lip_i = rounded_rect(X0 + 1.45, X1 - 1.45, Y0 + 1.45, Y1 - 1.45, 0.5)
lip = prism(lip_o, 0, 3.0) - prism(lip_i, 0, 3.0)
for bx, by in BOSSES:
    s = BOSS_R + 1.2                                              # square notch: no lip slivers
    lip -= box(bx - s, bx + s, by - s, by + s, -1, 4)
    base -= cyl(bx, by, -FLOOR - 1, 1, SCREW_CLR / 2)
    base -= cyl(bx, by, -FLOOR - 1, -FLOOR + 2.0, SCREW_HEAD / 2)  # head counterbore

base += lip

# PCB posts: 4 corners + middle of both short edges, 3.5 mm onto the board
for px, py in ((0.3, 0.3), (0.3, PCB_W - 3.8), (PCB_L - 3.8, 0.3),
               (PCB_L - 3.8, PCB_W - 3.8), (0.3, PCB_W / 2 - 2), (PCB_L - 3.8, PCB_W / 2 - 2)):
    base += box(px, px + 3.5, py, py + 3.5, 0, ZPCB)

# antenna bracket (jack clamps through it with its nut + washer) and two cradles
base += box(BRACKET_X, BRACKET_X + 3.0, PCB_W + GAP_SIDE + 0.3, Y1 - 0.3, 0, ANT_Z + 7)
base -= cyl_x(BRACKET_X - 1, BRACKET_X + 4, ANT_Y, ANT_Z, SMA_HOLE / 2)
for cx in (44.0, 70.0):
    c = box(cx, cx + 6, PCB_W + GAP_SIDE + 0.5, Y1 - 0.5, 0, ANT_Z - 2.5)   # below the axis: blunt edges
    c -= cyl_x(cx - 1, cx + 7, ANT_Y, ANT_Z, 5.2)                 # 10 mm whip seat
    c -= box(cx + 1.5, cx + 4.5, Y0, Y1, 0, 1.6)                  # cable-tie tunnel
    base += c

# =========================================================== fit-check dummies
pcb = box(0, PCB_L, 0, PCB_W, ZPCB, ZPCB + PCB_T)
cell = cyl_y(1.0, PCB_W - 1.0, 91.0, ZPCB + CELL_TOP - 9.25, 9.25)
underside = box(5, PCB_L - 5, 5, PCB_W - 5, ZPCB - 4.71, ZPCB)  # pins + wires zone
antenna = cyl_x(ANT_X0, ANT_X1, ANT_Y, ANT_Z, 5.0)
knob = box(SW_X0, SW_X1, -SW_SPACE, 0, ZPCB + PCB_T + SW_TOP - 5.2, ZPCB + PCB_T + SW_TOP)
usb = box(USB_X0, USB_X0 + USB_W, -0.5, 7, ZPCB + PCB_T + USB_Z0, ZPCB + PCB_T + USB_Z0 + USB_H)

print("outer size: %.1f x %.1f x %.1f mm" % (OX1 - OX0, OY1 - OY0, FLOOR + ZTOP_IN + TOP))
checks = {"pcb": pcb, "cell": cell, "pins/wires": underside, "antenna": antenna,
          "switch knob": knob, "usb socket": usb}
for name, body in checks.items():
    for part_name, part in (("lid", lid), ("base", base)):
        v = (body ^ part).volume()
        print(f"  {name:11s} vs {part_name:4s}: overlap {v:7.2f} mm^3")
print("  lid vs base: overlap %.2f mm^3" % (lid ^ base).volume())
print("  logo inlay vs lid: overlap %.2f mm^3" % (logo_inlay ^ lid).volume())
print("  red vs orange inlay: overlap %.2f mm^3" % (logo_mark ^ logo_text).volume())


def save(m, name):
    """STL for the slicer; indexed OBJ at full precision for the design studio (STL's float32
    can fold nearly-coincident vertices together and open the mesh)."""
    mesh = m.to_mesh()
    mesh.merge()
    t = trimesh.Trimesh(mesh.vert_properties[:, :3].astype(np.float64), mesh.tri_verts, process=False)
    t.export(OUT / f"{name}.stl")
    t.export(OUT / f"{name}.obj", digits=8)


save(lid, "nazer_lid")
save(logo_mark, "nazer_lid_logo_mark")
save(logo_text, "nazer_lid_logo_text")
save(base, "nazer_base")
save(pcb + cell + antenna + knob + usb, "fitcheck_dummies")
print("written to", OUT)
