"""Parametric Kiku enclosure prototype.

The coordinate system follows Mechanical/reference/board_datums.json:
X = 0..60 mm left-to-right across the PCB, Y = 0..105 mm bottom-to-top,
Z = 0 mm at the PCB rear face and Z = 1.6 mm at the PCB front face.

This is intentionally a first-print enclosure, not a frozen mould-tool model.  The
battery, LCD stack adhesive, FM coax bend and kickstand fit remain parameters so
they can be corrected after physical fit checks without redrawing the housing.
"""

from pathlib import Path
import json
import math

import cadquery as cq
import trimesh


ROOT = Path(__file__).resolve().parents[2]
MECH = ROOT / "Mechanical"
OUT = MECH / "exports"
REVIEW = MECH / "review"
DATUMS = MECH / "reference" / "board_datums.json"
PCB_STEP = MECH / "reference" / "pcb_populated.step"

OUT.mkdir(parents=True, exist_ok=True)
REVIEW.mkdir(parents=True, exist_ok=True)


# -----------------------------------------------------------------------------
# Primary product parameters (mm)
# -----------------------------------------------------------------------------
PCB_W = 60.0
PCB_H = 105.0
PCB_T = 1.6

CASE_W = 69.0
CASE_H = 115.0
CASE_CX = PCB_W / 2.0
CASE_CY = PCB_H / 2.0
CASE_R = 11.0
WALL = 1.8

# V8 appearance-first envelope.  The 10 mm battery is absorbed into a uniformly
# deeper rear shell instead of a local hump, producing one continuous back surface.
REAR_Z0 = -13.5
REAR_BACK_WALL = 3.0
SEAM_Z = 0.5
SEAM_GAP = 0.12
FRONT_Z1 = 15.2

# Display from HS20HS072RX mechanical drawing / current KiCad placement.
LCD_CX = 30.01
LCD_CY = 82.885
LCD_BODY_W = 51.8
LCD_BODY_H = 36.2
LCD_ACTIVE_W = 40.8
LCD_ACTIVE_H = 30.6
LENS_W = 54.0
LENS_H = 39.6
LENS_R = 5.8
LENS_T = 0.8

# Current PCB controls.
BTN1 = (16.0, 55.0)
BTN2 = (44.0, 55.0)
ENC = (30.0, 40.0)

# V8 control language: two soft "pebble" button caps and a rounded encoder puck.
# The button caps are installed from the outside after the PCB/front shell are in
# place.  A split square socket grips the real 7.2 x 7.2 mm actuator on SW3/SW4,
# rather than relying on gravity or a loose centre stem.  The slight interference
# is intentional and the split socket provides compliance for PA12/FDM prototypes.
BUTTON_APERTURE = 10.4
BUTTON_HEAD_W = 14.0
BUTTON_HEAD_H = 11.6
BUTTON_HEAD_Z0 = FRONT_Z1 - 0.05
BUTTON_HEAD_Z1 = FRONT_Z1 + 2.85
BUTTON_SOCKET_OUTER = 9.4
BUTTON_SOCKET_INNER = 7.05
BUTTON_SOCKET_Z0 = 12.0
BUTTON_SOCKET_Z1 = BUTTON_HEAD_Z0 + 0.15
BUTTON_SOCKET_ROOF_Z = 13.72
BUTTON_SOCKET_SPLIT = 0.65
BUTTON_LEFT_ANGLE = -7.0
BUTTON_RIGHT_ANGLE = 7.0
BUTTON_RECESS_W = 15.2
BUTTON_RECESS_H = 12.8
BUTTON_RECESS_DEPTH = 0.60

# PEC11L-4120F-S0020 uses a 6.0 mm, 18-tooth knurled shaft and M7x0.75 bushing.
# The V8 knob uses a compliant split-collet bore instead of a loose cylindrical
# hole.  A shallow nut recess under the knob also allows the encoder bushing/nut
# to mechanically support the front shell if desired, reducing load on soldered
# encoder tabs during repeated push-to-select use.
ENCODER_SHAFT_D = 6.0
ENCODER_BORE_D = 5.75
ENCODER_LEADIN_D = 6.35
ENCODER_KNOB_D = 20.0
ENCODER_KNOB_Z0 = FRONT_Z1 + 0.10
ENCODER_KNOB_Z1 = 22.35
ENCODER_COLLET_SLOT_W = 0.70
ENCODER_COLLET_SLOT_Z1 = FRONT_Z1 + 2.9
ENCODER_NUT_RECESS_D = 12.4
ENCODER_NUT_RECESS_DEPTH = 1.1

# Current edge I/O.
USB = (20.09, 2.50)
HEADPHONE = (7.57, 4.51)
MICROSD = (36.965, 96.35)
FM_UFL = (57.395, 66.55)

# V8 hides the only service fasteners underneath the removable screen lens. They
# reuse the PCB's two upper 2.2 mm mounting holes, so there are no extra holes in
# the PCB and no visible screw heads on the finished exterior.
CASE_SCREWS = [(6.0, 99.0), (54.0, 99.0)]
CASE_SCREW_CLEARANCE_D = 2.4
CASE_SCREW_HEAD_D = 4.6
CASE_SCREW_HEAD_FLOOR_Z = 12.95
CASE_INSERT_D = 3.25
CASE_INSERT_DEPTH = 3.6

# Softer microSD access: a narrower card tunnel plus a broad thumb scallop at the
# top edge instead of the previous rectangular mouth.
SD_SLOT_W = 16.5
SD_SLOT_H = 7.0
SD_SCOOP_W = 24.0
SD_SCOOP_R = 6.8
SD_SCOOP_Y = 112.3
SD_SCOOP_Z = 3.0

# Rear packaging reserves.
# User-specified LiPo: 10 x 34 x 50 mm. Capacity is recorded separately because
# the housing is driven by the physical envelope rather than the printed mAh.
BATTERY_CX = 30.0
BATTERY_CY = 70.0
BATTERY_W = 34.0
BATTERY_H = 50.0
BATTERY_T = 10.0
BATTERY_CAPACITY_MAH = 200

# V8 removes the internal speaker completely.  The existing LS1 PicoBlade output
# remains available electrically; a small right-side cable egress lets an external
# speaker lead leave the enclosure without reintroducing a front/rear speaker bulge.
EXTERNAL_SPEAKER_ONLY = True
SPEAKER_EGRESS_Y = 26.4
SPEAKER_EGRESS_Z = 4.2
SPEAKER_EGRESS_D = 4.4

# Recessed multimeter-style kickstand.  It lies nearly flush in the back when
# closed and rotates outward about an X-axis hinge.  A short piece of 1.75 mm
# filament can be used as the hinge pin through the nominal 1.9 mm bore.
STAND_CX = 30.0
STAND_CY = 56.0
STAND_W = 42.0
STAND_H = 56.0
STAND_FRAME = 4.0
STAND_R = 6.0
STAND_T = 1.4
STAND_RECESS_W = 48.0
STAND_RECESS_H = 64.5
STAND_RECESS_R = 7.5
STAND_RECESS_DEPTH = 1.7
STAND_HINGE_Y = 87.0
STAND_HINGE_Z = -11.6
STAND_BARREL_R = 2.0
STAND_EAR_R = 2.3
STAND_PIN_D = 1.9
STAND_OPEN_ANGLE_DEG = 62.0

# FM antenna hardware from the ordered AliExpress parts.
# Pigtail listing: RF1.13 U.FL/IPX -> SMA bulkhead, 100 mm option.
# Telescopic antenna listing drawing: 105 mm collapsed, 310 mm extended, 9 mm dia.
FM_COAX_LENGTH = 100.0
FM_COAX_D = 1.13
FM_COAX_BEND_R = 5.0
SMA_THREAD_D = 6.35
SMA_HOLE_D = 6.8
SMA_BOSS_D = 12.0
SMA_CENTER_Y = 18.0
SMA_CENTER_Z = -5.6
ANTENNA_COLLAPSED = 105.0
ANTENNA_EXTENDED = 310.0
ANTENNA_D = 9.0
# The hinged rod portion stows up the left side.  The connector/hinge occupies
# the balance of the 105 mm collapsed total length along the X direction.
ANTENNA_SIDE_STOW_Y0 = 18.0
ANTENNA_SIDE_STOW_Y1 = 107.0
ANTENNA_SIDE_STOW_X = -9.2
ANTENNA_SIDE_STOW_Z = -5.6
ANTENNA_CRADLE_YS = (48.0, 84.0)
ANTENNA_CRADLE_LENGTH = 6.0
ANTENNA_CRADLE_OUTER_R = 5.5
ANTENNA_CRADLE_CLEARANCE_R = ANTENNA_D / 2.0 + 0.35
ANTENNA_CRADLE_BODY_X = -7.2

# 2-D centreline below the battery.  It starts beside J3 at the right PCB edge,
# routes downward, then crosses left only after leaving the BM83 lower-right RF
# keepout.  The resulting ~90 mm route leaves several millimetres of service slack.
FM_COAX_ROUTE = [
    (60.4, 66.55),
    (60.4, 43.0),
    (50.0, 40.0),
    (32.0, 34.0),
    (16.0, 27.0),
    (1.0, 18.0),
]

# Explicit lower-right radio keepout around the BM83 antenna end.
RF_KEEP_X0 = 37.0
RF_KEEP_Y0 = 0.0
RF_KEEP_W = 27.0
RF_KEEP_H = 31.0

# Four existing M2 PCB mounting holes in mechanical coordinates.
MOUNT_HOLES = [(6.0, 37.0), (54.0, 37.0), (6.0, 99.0), (54.0, 99.0)]


def rounded_prism(w: float, h: float, r: float, z0: float, z1: float,
                  cx: float = CASE_CX, cy: float = CASE_CY) -> cq.Workplane:
    """Robust rounded-rectangle prism made from boxes + corner cylinders."""
    if z1 <= z0:
        raise ValueError("z1 must be greater than z0")
    r = min(r, w / 2.0, h / 2.0)
    d = z1 - z0
    zc = (z0 + z1) / 2.0
    shape = cq.Workplane("XY").box(w - 2.0 * r, h, d).translate((cx, cy, zc))
    shape = shape.union(cq.Workplane("XY").box(w, h - 2.0 * r, d).translate((cx, cy, zc)))
    for sx in (-1.0, 1.0):
        for sy in (-1.0, 1.0):
            x = cx + sx * (w / 2.0 - r)
            y = cy + sy * (h / 2.0 - r)
            shape = shape.union(cq.Workplane("XY").circle(r).extrude(d).translate((x, y, z0)))
    return shape


def rounded_cut(w: float, h: float, r: float, z0: float, z1: float,
                cx: float, cy: float) -> cq.Workplane:
    return rounded_prism(w, h, r, z0, z1, cx, cy)


def cyl_x(x0: float, x1: float, radius: float, y: float, z: float) -> cq.Workplane:
    """Cylinder along X."""
    return (
        cq.Workplane("YZ")
        .workplane(offset=x0)
        .center(y, z)
        .circle(radius)
        .extrude(x1 - x0)
    )


def cyl_y(y0: float, y1: float, radius: float, x: float, z: float) -> cq.Workplane:
    """Cylinder along global +Y from y0 to y1.

    Do not implement this with Workplane("XZ"): CadQuery's XZ plane normal is
    -Y, which silently mirrors the requested Y interval and can leave an edge
    connector opening capped by the enclosure wall.
    """
    if y1 <= y0:
        raise ValueError("y1 must be greater than y0")
    solid = cq.Solid.makeCylinder(
        radius,
        y1 - y0,
        cq.Vector(x, y0, z),
        cq.Vector(0.0, 1.0, 0.0),
    )
    return cq.Workplane(obj=solid)


def make_headphone_clearance() -> cq.Workplane:
    """3.5 mm jack opening plus clearance for a normal plug overmould.

    The transformed J6 STEP mouth is at Y=-2.29 mm with its bore centred at
    X=7.57, Z=4.10 mm.  The large outer relief crosses the case's Y=-5 mm
    outside face and overlaps the connector mouth so no printed skin can cap it.
    A smaller continuation prevents an internal lip around the plug shaft.
    """
    overmould = cyl_y(-6.5, -1.9, 5.5, HEADPHONE[0], 4.1)
    shaft = cyl_y(-2.1, 4.5, 3.25, HEADPHONE[0], 4.1)
    return overmould.union(shaft)


def cylinder_x(x0: float, x1: float, radius: float, y: float, z: float) -> cq.Workplane:
    """Cylinder along global +X using an explicit OCC direction."""
    if x1 <= x0:
        raise ValueError("x1 must be greater than x0")
    solid = cq.Solid.makeCylinder(
        radius,
        x1 - x0,
        cq.Vector(x0, y, z),
        cq.Vector(1.0, 0.0, 0.0),
    )
    return cq.Workplane(obj=solid)


def make_sma_bulkhead_clearance() -> cq.Workplane:
    """6.8 mm through-hole for the side-mounted SMA bulkhead."""
    return cylinder_x(-6.8, 2.8, SMA_HOLE_D / 2.0, SMA_CENTER_Y, SMA_CENTER_Z)


def make_external_speaker_egress() -> cq.Workplane:
    """Right-side cable passage for the external speaker lead from LS1."""
    return cylinder_x(58.0, 66.0, SPEAKER_EGRESS_D / 2.0,
                      SPEAKER_EGRESS_Y, SPEAKER_EGRESS_Z)


def make_sd_access() -> cq.Workplane:
    """MicroSD card tunnel plus a broad, soft thumb scallop at the top edge."""
    slot = rounded_cut(
        SD_SLOT_W, SD_SLOT_H, 2.1,
        -1.4, 5.6,
        MICROSD[0], 108.0,
    )
    scoop = cyl_x(
        MICROSD[0] - SD_SCOOP_W / 2.0,
        MICROSD[0] + SD_SCOOP_W / 2.0,
        SD_SCOOP_R,
        SD_SCOOP_Y,
        SD_SCOOP_Z,
    )
    return slot.union(scoop)


def make_antenna_cradle(y: float) -> cq.Workplane:
    """Open crescent saddle for the folded 9 mm telescopic antenna.

    The SMA hinge still carries the antenna mechanically; these two soft saddles
    simply nest the rod against the case so it does not rattle or look accidental.
    They are deliberately open to the left so no brittle snap-fit is required.
    """
    y0 = y - ANTENNA_CRADLE_LENGTH / 2.0
    y1 = y + ANTENNA_CRADLE_LENGTH / 2.0
    outer = cyl_y(
        y0, y1, ANTENNA_CRADLE_OUTER_R,
        ANTENNA_CRADLE_BODY_X, ANTENNA_SIDE_STOW_Z,
    )
    clearance = cyl_y(
        y0 - 0.2, y1 + 0.2, ANTENNA_CRADLE_CLEARANCE_R,
        ANTENNA_SIDE_STOW_X, ANTENNA_SIDE_STOW_Z,
    )
    return outer.cut(clearance)


def coax_route_length() -> float:
    return sum(
        math.hypot(x1 - x0, y1 - y0)
        for (x0, y0), (x1, y1) in zip(FM_COAX_ROUTE, FM_COAX_ROUTE[1:])
    )


def make_coax_route_keepout() -> cq.Workplane:
    """Compound representing the reserved RF1.13 centreline/clearance."""
    z = REAR_Z0 + WALL + 1.25
    solids = []
    radius = 1.15
    for (x0, y0), (x1, y1) in zip(FM_COAX_ROUTE, FM_COAX_ROUTE[1:]):
        dx = x1 - x0
        dy = y1 - y0
        length = math.hypot(dx, dy)
        solids.append(cq.Solid.makeCylinder(
            radius,
            length,
            cq.Vector(x0, y0, z),
            cq.Vector(dx / length, dy / length, 0.0),
        ))
    return cq.Workplane(obj=cq.Compound.makeCompound(solids))


def coax_clip(x: float, y: float) -> cq.Workplane:
    """Small PA12-friendly bridge clip for 1.13 mm coax on the rear cavity floor."""
    floor = REAR_Z0 + REAR_BACK_WALL - 0.05
    left = cq.Workplane("XY").box(0.8, 2.4, 2.0).translate((x - 1.9, y, floor + 1.0))
    right = cq.Workplane("XY").box(0.8, 2.4, 2.0).translate((x + 1.9, y, floor + 1.0))
    bridge = cq.Workplane("XY").box(4.6, 2.4, 0.7).translate((x, y, floor + 2.35))
    return left.union(right).union(bridge)


def circular_prism(diameter: float, z0: float, z1: float,
                   cx: float, cy: float) -> cq.Workplane:
    """Cylinder on XY with its lower face at z0."""
    if z1 <= z0:
        raise ValueError("z1 must be greater than z0")
    return (
        cq.Workplane("XY")
        .circle(diameter / 2.0)
        .extrude(z1 - z0)
        .translate((cx, cy, z0))
    )


def make_front() -> cq.Workplane:
    outer = rounded_prism(CASE_W, CASE_H, CASE_R,
                          SEAM_Z + SEAM_GAP / 2.0, FRONT_Z1)
    inner = rounded_prism(
        CASE_W - 2.0 * WALL,
        CASE_H - 2.0 * WALL,
        CASE_R - WALL,
        SEAM_Z - 0.4,
        FRONT_Z1 - WALL,
    )
    front = outer.cut(inner)

    # Flush lens island and active-view aperture.
    lens_recess = rounded_cut(LENS_W + 0.35, LENS_H + 0.35, LENS_R + 0.15,
                             FRONT_Z1 - LENS_T, FRONT_Z1 + 0.2, LCD_CX, LCD_CY)
    aperture = rounded_cut(LCD_ACTIVE_W + 1.8, LCD_ACTIVE_H + 1.8, 2.8,
                           FRONT_Z1 - WALL - 0.5, FRONT_Z1 + 0.5, LCD_CX, LCD_CY)
    front = front.cut(lens_recess).cut(aperture)

    # Two M2 service screws are hidden completely underneath the removable screen
    # lens and reuse the PCB's existing upper 2.2 mm mounting holes.  A low-profile
    # M2 head sits below the lens underside; no screw head is visible once the lens
    # is installed.
    for x, y in CASE_SCREWS:
        through = circular_prism(
            CASE_SCREW_CLEARANCE_D,
            FRONT_Z1 - WALL - 1.0,
            FRONT_Z1 + 0.5,
            x, y,
        )
        head = circular_prism(
            CASE_SCREW_HEAD_D,
            CASE_SCREW_HEAD_FLOOR_Z,
            FRONT_Z1 + 0.5,
            x, y,
        )
        front = front.cut(through).cut(head)

    # Blobject tactile controls.  The visible caps are deliberately wider than
    # the through-openings and sit in shallow pebble-shaped moats.  Their hidden
    # split sockets pass through the 10.4 mm openings and grip the real 7.2 mm
    # square actuator, so the caps are retained after assembly rather than merely
    # balancing on the switch stem.
    for (x, y), angle in ((BTN1, BUTTON_LEFT_ANGLE), (BTN2, BUTTON_RIGHT_ANGLE)):
        recess = rounded_cut(
            BUTTON_RECESS_W, BUTTON_RECESS_H, BUTTON_RECESS_H / 2.0 - 0.2,
            FRONT_Z1 - BUTTON_RECESS_DEPTH, FRONT_Z1 + 0.25,
            x, y,
        ).rotate((x, y, 0.0), (x, y, 1.0), angle)
        aperture = rounded_cut(
            BUTTON_APERTURE, BUTTON_APERTURE, 2.4,
            FRONT_Z1 - WALL - 0.8, FRONT_Z1 + 0.6,
            x, y,
        )
        front = front.cut(recess).cut(aperture)

    # Recessed encoder dial.  The outer dish hides the underside of the knob and
    # leaves axial room for the PEC11L push action.  A second shallow centre recess
    # provides space for a low-profile M7x0.75 nut/washer under the knob, allowing
    # the encoder bushing to support the shell mechanically if desired.
    dial_recess = circular_prism(
        21.4, FRONT_Z1 - 0.85, FRONT_Z1 + 0.25, ENC[0], ENC[1]
    )
    nut_recess = circular_prism(
        ENCODER_NUT_RECESS_D,
        FRONT_Z1 - ENCODER_NUT_RECESS_DEPTH,
        FRONT_Z1 + 0.30,
        ENC[0], ENC[1],
    )
    shaft = circular_prism(
        7.4, FRONT_Z1 - WALL - 0.8, FRONT_Z1 + 0.5, ENC[0], ENC[1]
    )
    front = front.cut(dial_recess).cut(nut_recess).cut(shaft)

    # V8 external-speaker lead egress.  The internal C49247039 package and all
    # grille/pod geometry are intentionally removed for a clean front surface.
    front = front.cut(make_external_speaker_egress())

    # Bottom I/O openings: USB-C rounded slot and 3.5 mm jack.
    usb_slot = rounded_cut(10.6, 8.5, 1.8, 1.2, 6.3, USB[0], -2.4)
    jack_hole = make_headphone_clearance()
    front = front.cut(usb_slot).cut(jack_hole)

    # Softer microSD access: narrow card tunnel plus a broad thumb scallop rather
    # than the previous rectangular opening.
    front = front.cut(make_sd_access())

    return front


def make_stand() -> cq.Workplane:
    """Recessed multimeter-style rear kickstand, printed as a separate part."""
    outer = rounded_prism(
        STAND_W, STAND_H, STAND_R,
        REAR_Z0, REAR_Z0 + STAND_T,
        STAND_CX, STAND_CY,
    )
    inner = rounded_prism(
        STAND_W - 2.0 * STAND_FRAME,
        STAND_H - 2.0 * STAND_FRAME,
        max(1.5, STAND_R - STAND_FRAME),
        REAR_Z0 - 0.1, REAR_Z0 + STAND_T + 0.1,
        STAND_CX, STAND_CY,
    )
    frame = outer.cut(inner)

    # Two short hinge barrels sit just outside the battery's X envelope.  This
    # lets the hinge axis tuck into the rear wall without a large barrel running
    # across the battery area or protruding from the smooth back.
    barrel_l = cylinder_x(9.2, 12.5, STAND_BARREL_R,
                          STAND_HINGE_Y, STAND_HINGE_Z)
    barrel_r = cylinder_x(47.5, 50.8, STAND_BARREL_R,
                          STAND_HINGE_Y, STAND_HINGE_Z)
    neck_z = REAR_Z0 + STAND_T / 2.0
    neck_l = cq.Workplane("XY").box(4.0, 4.0, STAND_T).translate((11.0, 84.0, neck_z))
    neck_r = cq.Workplane("XY").box(4.0, 4.0, STAND_T).translate((49.0, 84.0, neck_z))
    stand = frame.union(barrel_l).union(barrel_r).union(neck_l).union(neck_r)

    pin = cylinder_x(8.7, 51.3, STAND_PIN_D / 2.0,
                     STAND_HINGE_Y, STAND_HINGE_Z)
    return stand.cut(pin)


def make_rear() -> cq.Workplane:
    outer = rounded_prism(CASE_W, CASE_H, CASE_R, REAR_Z0,
                          SEAM_Z - SEAM_GAP / 2.0)
    cavity = rounded_prism(
        CASE_W - 2.0 * WALL,
        CASE_H - 2.0 * WALL,
        CASE_R - WALL,
        REAR_Z0 + REAR_BACK_WALL,
        SEAM_Z + 0.4,
    )
    rear = outer.cut(cavity)

    # Locating tongue inside the front-shell cavity.  A short shoulder bridges
    # the tongue into the rear wall; the tongue itself has ~0.175 mm clearance
    # per side from the front cavity for a realistic first-print slip fit.
    front_inner_w = CASE_W - 2.0 * WALL
    front_inner_h = CASE_H - 2.0 * WALL
    front_inner_r = CASE_R - WALL
    tongue_outer_w = front_inner_w - 0.35
    tongue_outer_h = front_inner_h - 0.35
    tongue_outer_r = front_inner_r - 0.18
    tongue_wall = 1.0
    tongue_outer = rounded_prism(tongue_outer_w, tongue_outer_h, tongue_outer_r,
                                 SEAM_Z - 0.15, SEAM_Z + 1.0)
    tongue_inner = rounded_prism(tongue_outer_w - 2.0 * tongue_wall,
                                 tongue_outer_h - 2.0 * tongue_wall,
                                 tongue_outer_r - tongue_wall,
                                 SEAM_Z - 0.25, SEAM_Z + 1.1)
    tongue = tongue_outer.cut(tongue_inner)

    shoulder_outer = rounded_prism(front_inner_w + 0.8, front_inner_h + 0.8,
                                   front_inner_r + 0.4,
                                   SEAM_Z - 0.30, SEAM_Z - SEAM_GAP / 2.0)
    shoulder_inner = rounded_prism(tongue_outer_w - 2.0 * tongue_wall,
                                   tongue_outer_h - 2.0 * tongue_wall,
                                   tongue_outer_r - tongue_wall,
                                   SEAM_Z - 0.35, SEAM_Z + 0.05)
    rear = rear.union(shoulder_outer.cut(shoulder_inner)).union(tongue)

    # Rounded case corners bring the tongue slightly inside the nominal PCB
    # rectangle at the board corners.  Carve an explicit PCB insertion envelope
    # so the real board cannot scrape the locating lip during assembly.
    pcb_insert_clearance = (
        cq.Workplane("XY")
        .box(PCB_W + 0.6, PCB_H + 0.6, 1.85)
        .translate((PCB_W / 2.0, PCB_H / 2.0, 0.975))
    )
    rear = rear.cut(pcb_insert_clearance)

    # V8 appearance-first rear: no battery hump.  The whole back is deep enough
    # for the 10 mm LiPo plus a robust back wall and recessed kickstand.

    # Recess the kickstand into the rear surface.  The recess is shallower than
    # the 3.0 mm back wall, so it never opens into the battery cavity.
    stand_recess = rounded_prism(
        STAND_RECESS_W, STAND_RECESS_H, STAND_RECESS_R,
        REAR_Z0 - 0.2, REAR_Z0 + STAND_RECESS_DEPTH,
        STAND_CX, STAND_CY,
    )
    rear = rear.cut(stand_recess)
    # Fingernail relief at the bottom of the recess.
    finger_relief = circular_prism(
        10.0, REAR_Z0 - 0.2, REAR_Z0 + STAND_RECESS_DEPTH,
        STAND_CX, STAND_CY - STAND_RECESS_H / 2.0,
    )
    rear = rear.cut(finger_relief)

    # Clearance pockets for the stand's two short barrels.  The middle of the
    # rear wall remains intact behind the battery.
    rear = rear.cut(cylinder_x(
        8.8, 12.9, STAND_BARREL_R + 0.25,
        STAND_HINGE_Y, STAND_HINGE_Z,
    ))
    rear = rear.cut(cylinder_x(
        47.1, 51.2, STAND_BARREL_R + 0.25,
        STAND_HINGE_Y, STAND_HINGE_Z,
    ))

    # Outboard hinge ears for the stand.  Their inward extent remains outside the
    # battery cavity because the hinge sits in the thick rear wall.
    left_ear = cylinder_x(4.5, 8.8, STAND_EAR_R,
                          STAND_HINGE_Y, STAND_HINGE_Z)
    right_ear = cylinder_x(51.2, 55.5, STAND_EAR_R,
                           STAND_HINGE_Y, STAND_HINGE_Z)
    rear = rear.union(left_ear).union(right_ear)
    stand_pin_clearance = cylinder_x(4.0, 56.0, STAND_PIN_D / 2.0,
                                     STAND_HINGE_Y, STAND_HINGE_Z)
    rear = rear.cut(stand_pin_clearance)

    # PCB standoffs use the actual four board holes.  The upper pair also receive
    # the V8 enclosure screws hidden beneath the front lens; their 3.25 mm pockets
    # are sized for small M2 brass inserts / printed-thread experiments.  The lower
    # pair remain ordinary locating/support posts.
    for x, y in MOUNT_HOLES:
        # Overlap the rear floor by 0.3 mm.  The previous -6.5 mm start sat
        # 0.2 mm above the -6.7 mm cavity floor and exported four floating
        # solids instead of printable standoffs.
        post_z0 = REAR_Z0 + REAR_BACK_WALL - 0.3
        post = cq.Workplane("XY").circle(3.0).extrude(-0.1 - post_z0).translate((x, y, post_z0))
        rear = rear.union(post)
        if (x, y) in CASE_SCREWS:
            insert = circular_prism(
                CASE_INSERT_D,
                -CASE_INSERT_DEPTH - 0.15,
                0.55,
                x, y,
            )
            rear = rear.cut(insert)
        else:
            pilot_z0 = REAR_Z0 + REAR_BACK_WALL + 0.4
            pilot = cq.Workplane("XY").circle(1.10).extrude(0.5 - pilot_z0).translate((x, y, pilot_z0))
            rear = rear.cut(pilot)

    # Compliant lower-board cradles support the screw-free bottom 37 mm.
    for x in (0.2, 59.8):
        # Extend each rail into the side wall so it is integral with the shell.
        # A 6.4 mm width gives ~0.3 mm positive overlap with the nominal inner
        # wall while leaving the board insertion volume untouched above Z=0.
        rail = rounded_prism(6.4, 17.0, 0.7, -1.0, -0.08, x, 20.0)
        rear = rear.union(rail)

    # Bottom connector openings must be present in both halves at the seam.
    usb_slot = rounded_cut(10.6, 8.5, 1.8, -1.3, 5.8, USB[0], -2.4)
    jack_hole = make_headphone_clearance()
    rear = rear.cut(usb_slot).cut(jack_hole)

    # Matching microSD scallop through the rear half.
    rear = rear.cut(make_sd_access())

    # Left-side SMA bulkhead.  The local boss creates a flat/reinforced wall for
    # the nut while keeping metal hardware away from the BM83 lower-right keepout.
    sma_boss = cylinder_x(-5.7, -0.2, SMA_BOSS_D / 2.0,
                          SMA_CENTER_Y, SMA_CENTER_Z)
    rear = rear.union(sma_boss).cut(make_sma_bulkhead_clearance())

    # Two soft external saddles intentionally make the folded antenna look parked,
    # rather than like a loose whip hanging off the SMA hinge.  The cradle bore is
    # larger than the 9 mm antenna body, so it guides/nests instead of clamping.
    for y in ANTENNA_CRADLE_YS:
        rear = rear.union(make_antenna_cradle(y))

    # J3 is only 2.6 mm from the right PCB edge.  Open a small internal seam notch
    # so the U.FL lead can pass around the board edge without being pinched.
    j3_edge_pass = (
        cq.Workplane("XY").box(5.0, 8.0, 4.5)
        .translate((61.2, FM_UFL[1], 0.7))
    )
    rear = rear.cut(j3_edge_pass)

    # Low-profile coax retention clips follow the reserved ~90 mm route.  This
    # keeps the 100 mm RF1.13 lead below the battery and out of the Bluetooth RF
    # keepout while leaving a few millimetres of service slack near J3.
    for x, y in ((59.6, 58.0), (59.6, 46.0), (48.0, 39.3),
                 (31.5, 33.8), (15.5, 26.8)):
        rear = rear.union(coax_clip(x, y))

    return rear


def make_lens() -> cq.Workplane:
    return rounded_prism(LENS_W, LENS_H, LENS_R,
                         FRONT_Z1 - LENS_T, FRONT_Z1, LCD_CX, LCD_CY)


def make_button_cap(x: float, y: float, angle: float = 0.0) -> cq.Workplane:
    """Soft pebble cap with a compliant square socket for SW3/SW4.

    Assembly is intentionally simple: install the populated PCB/front shell, then
    press the cap through the front opening onto the switch's 7.2 mm square
    actuator.  The four split socket walls flex around the actuator and provide
    positive friction retention.  No glue, loose centre post or hidden retainer
    plate is required for the first prototype.
    """
    # Three-section elliptical loft gives the top a soft, slightly domed pebble
    # profile rather than a generic square keycap.
    face = (
        cq.Workplane("XY")
        .workplane(offset=BUTTON_HEAD_Z0)
        .center(x, y)
        .ellipse(BUTTON_HEAD_W / 2.0, BUTTON_HEAD_H / 2.0)
        .workplane(offset=1.75)
        .ellipse(BUTTON_HEAD_W / 2.0 - 0.20, BUTTON_HEAD_H / 2.0 - 0.20)
        .workplane(offset=BUTTON_HEAD_Z1 - BUTTON_HEAD_Z0 - 1.75)
        .ellipse(BUTTON_HEAD_W / 2.0 - 0.85, BUTTON_HEAD_H / 2.0 - 0.85)
        .loft(combine=True)
        .rotate((x, y, 0.0), (x, y, 1.0), angle)
    )

    socket = rounded_prism(
        BUTTON_SOCKET_OUTER, BUTTON_SOCKET_OUTER, 1.4,
        BUTTON_SOCKET_Z0, BUTTON_SOCKET_Z1,
        x, y,
    )
    # Slightly undersized upper socket for a controlled press fit, with a wider
    # lead-in at the bottom so the cap self-centres on the tactile actuator.
    socket_inner = rounded_cut(
        BUTTON_SOCKET_INNER, BUTTON_SOCKET_INNER, 0.65,
        BUTTON_SOCKET_Z0 - 0.2, BUTTON_SOCKET_ROOF_Z,
        x, y,
    )
    lead_in = rounded_cut(
        7.65, 7.65, 0.8,
        BUTTON_SOCKET_Z0 - 0.25, BUTTON_SOCKET_Z0 + 0.55,
        x, y,
    )
    cap = face.union(socket).cut(socket_inner).cut(lead_in)

    # Four hidden compliance splits turn the socket into four spring fingers.
    # Slots stop below the visible pebble head, so the front remains visually clean.
    split_z0 = BUTTON_SOCKET_Z0 - 0.1
    split_z1 = min(BUTTON_HEAD_Z0 - 0.35, BUTTON_SOCKET_Z0 + 2.35)
    split_h = split_z1 - split_z0
    split_zc = (split_z0 + split_z1) / 2.0
    wall_c = (BUTTON_SOCKET_OUTER / 2.0 + BUTTON_SOCKET_INNER / 2.0) / 2.0
    slot_ns = cq.Workplane("XY").box(
        BUTTON_SOCKET_SPLIT, 2.4, split_h
    ).translate((x, y + wall_c, split_zc))
    slot_ss = cq.Workplane("XY").box(
        BUTTON_SOCKET_SPLIT, 2.4, split_h
    ).translate((x, y - wall_c, split_zc))
    slot_e = cq.Workplane("XY").box(
        2.4, BUTTON_SOCKET_SPLIT, split_h
    ).translate((x + wall_c, y, split_zc))
    slot_w = cq.Workplane("XY").box(
        2.4, BUTTON_SOCKET_SPLIT, split_h
    ).translate((x - wall_c, y, split_zc))
    return cap.cut(slot_ns).cut(slot_ss).cut(slot_e).cut(slot_w)


def make_knob() -> cq.Workplane:
    """Soft encoder puck with hidden compliant collet for the PEC11L shaft."""
    knob = circular_prism(
        ENCODER_KNOB_D, ENCODER_KNOB_Z0, ENCODER_KNOB_Z1, ENC[0], ENC[1]
    )
    # Broad fillets make the encoder read as a single soft puck rather than a
    # machined cylinder while retaining a circular grip for rotary operation.
    knob = knob.edges("%Circle").fillet(1.25)

    # Blind 5.75 mm bore intentionally grips the 6.0 mm 18-tooth knurled shaft.
    # A 6.35 mm lead-in starts the fit squarely; the blind roof hides the shaft end.
    bore = circular_prism(
        ENCODER_BORE_D,
        ENCODER_KNOB_Z0 - 0.25,
        ENCODER_KNOB_Z1 - 0.75,
        ENC[0], ENC[1],
    )
    leadin = circular_prism(
        ENCODER_LEADIN_D,
        ENCODER_KNOB_Z0 - 0.30,
        ENCODER_KNOB_Z0 + 0.75,
        ENC[0], ENC[1],
    )
    knob = knob.cut(bore).cut(leadin)

    # Four short radial slots are hidden in the underside/recess and let the
    # lower part of the knob flex as a collet.  This makes the press fit tolerant
    # of both PA12 and ordinary FDM dimensional variation without a grub screw.
    slot_h = ENCODER_COLLET_SLOT_Z1 - (ENCODER_KNOB_Z0 - 0.15)
    slot_zc = (ENCODER_COLLET_SLOT_Z1 + ENCODER_KNOB_Z0 - 0.15) / 2.0
    radial_len = ENCODER_KNOB_D / 2.0 - ENCODER_BORE_D / 2.0 + 0.8
    radial_c = (ENCODER_KNOB_D / 2.0 + ENCODER_BORE_D / 2.0) / 2.0
    for dx, dy, sx, sy in (
        (radial_c, 0.0, radial_len, ENCODER_COLLET_SLOT_W),
        (-radial_c, 0.0, radial_len, ENCODER_COLLET_SLOT_W),
        (0.0, radial_c, ENCODER_COLLET_SLOT_W, radial_len),
        (0.0, -radial_c, ENCODER_COLLET_SLOT_W, radial_len),
    ):
        slot = cq.Workplane("XY").box(sx, sy, slot_h).translate(
            (ENC[0] + dx, ENC[1] + dy, slot_zc)
        )
        knob = knob.cut(slot)
    return knob


def make_keepouts() -> cq.Workplane:
    battery = rounded_prism(BATTERY_W, BATTERY_H, 6.0,
                            -(BATTERY_T + 0.15), -0.15, BATTERY_CX, BATTERY_CY)
    rf = rounded_prism(RF_KEEP_W, RF_KEEP_H, 3.0, -8.0, 14.5,
                       RF_KEEP_X0 + RF_KEEP_W / 2.0,
                       RF_KEEP_Y0 + RF_KEEP_H / 2.0)
    lcd = rounded_prism(LCD_BODY_W, LCD_BODY_H, 1.2,
                        2.0, 4.2, LCD_CX, LCD_CY)
    sma = cylinder_x(-8.0, 4.0, 5.0, SMA_CENTER_Y, SMA_CENTER_Z)
    antenna_stowed = cyl_y(ANTENNA_SIDE_STOW_Y0, ANTENNA_SIDE_STOW_Y1,
                           ANTENNA_D / 2.0 + 0.4,
                           ANTENNA_SIDE_STOW_X, ANTENNA_SIDE_STOW_Z)
    coax = make_coax_route_keepout()
    return cq.Workplane(obj=cq.Compound.makeCompound([
        battery.val(), rf.val(), lcd.val(),
        sma.val(), antenna_stowed.val(), coax.val()
    ]))


def export_part(name: str, part: cq.Workplane, stl: bool = True) -> None:
    cq.exporters.export(part, str(OUT / f"{name}.step"))
    if stl:
        stl_path = OUT / f"{name}.stl"
        cq.exporters.export(part, str(stl_path), tolerance=0.05, angularTolerance=0.15)
        # OCC occasionally emits a few zero-area triangles around heavily filleted
        # lofts. They do not affect the STEP solid but can make print services flag
        # the STL as non-watertight. Strip only degenerate faces and merge duplicate
        # vertices, then re-export the same surface mesh.
        mesh = trimesh.load_mesh(stl_path, process=True)
        mesh.update_faces(mesh.nondegenerate_faces())
        mesh.remove_unreferenced_vertices()
        mesh.merge_vertices()
        if not mesh.is_watertight or not mesh.is_winding_consistent:
            raise RuntimeError(f"{name} STL mesh failed watertight/winding QA")
        if len(mesh.split(only_watertight=False)) != 1:
            raise RuntimeError(f"{name} STL mesh exported as multiple components")
        mesh.export(stl_path)


def bbox_dict(part: cq.Workplane) -> dict:
    b = part.val().BoundingBox()
    return {
        "xmin": round(b.xmin, 3), "xmax": round(b.xmax, 3),
        "ymin": round(b.ymin, 3), "ymax": round(b.ymax, 3),
        "zmin": round(b.zmin, 3), "zmax": round(b.zmax, 3),
        "xlen": round(b.xlen, 3), "ylen": round(b.ylen, 3), "zlen": round(b.zlen, 3),
    }


def validate(name: str, part: cq.Workplane) -> dict:
    solid = part.val()
    solid_count = len(part.solids().vals())
    result = {
        "valid": bool(solid.isValid()),
        "solid_count": solid_count,
        "volume_mm3": round(solid.Volume(), 3),
        "bbox_mm": bbox_dict(part),
    }
    if not result["valid"] or result["volume_mm3"] <= 0 or solid_count != 1:
        raise RuntimeError(f"{name} failed solid validation: {result}")
    return result


def main() -> None:
    datums = json.loads(DATUMS.read_text())
    if abs(float(datums["board_thickness"]) - PCB_T) > 0.05:
        raise RuntimeError("PCB thickness changed; update enclosure parameters before export")

    front = make_front()
    rear = make_rear()
    lens = make_lens()
    b1 = make_button_cap(*BTN1, BUTTON_LEFT_ANGLE)
    b2 = make_button_cap(*BTN2, BUTTON_RIGHT_ANGLE)
    knob = make_knob()
    stand = make_stand()
    keepouts = make_keepouts()

    parts = {
        "kiku_front_shell_external_speaker_v8": front,
        "kiku_rear_shell_smooth_stand_v8": rear,
        "kiku_kickstand_v8": stand,
        "kiku_screen_lens_v8": lens,
        "kiku_button_left_v8": b1,
        "kiku_button_right_v8": b2,
        "kiku_encoder_knob_v8": knob,
    }
    report = {name: validate(name, part) for name, part in parts.items()}
    for name, part in parts.items():
        export_part(name, part)

    cq.exporters.export(keepouts, str(OUT / "kiku_packaging_keepouts_v8.step"))

    # CAD assembly compound.  The PCB reference stays a separate source-of-truth
    # STEP because its coordinate system/model coverage comes directly from KiCad.
    assembly = cq.Compound.makeCompound([
        front.val(), rear.val(), stand.val(), lens.val(), b1.val(), b2.val(), knob.val()
    ])
    cq.exporters.export(assembly, str(OUT / "kiku_housing_assembly_v8.step"))

    # Full fit-check assembly with the actual populated KiCad STEP.  This is the
    # file to open first in a mechanical CAD viewer when reviewing clearances.
    pcb = cq.importers.importStep(str(PCB_STEP))
    fit_check = cq.Compound.makeCompound([
        rear.val(), pcb.val(), front.val(), lens.val(),
        b1.val(), b2.val(), knob.val(), stand.val()
    ])
    cq.exporters.export(fit_check, str(OUT / "kiku_fit_check_with_pcb_v8.step"))

    # Open-stand reference at the intended desk angle.  This is a visual/clearance
    # reference only; the actual first prototype relies on hinge friction.
    stand_open = stand.rotate(
        (0.0, STAND_HINGE_Y, STAND_HINGE_Z),
        (1.0, STAND_HINGE_Y, STAND_HINGE_Z),
        STAND_OPEN_ANGLE_DEG,
    )
    stand_open_assembly = cq.Compound.makeCompound([
        rear.val(), front.val(), stand_open.val(), lens.val()
    ])
    cq.exporters.export(stand_open_assembly, str(OUT / "kiku_kickstand_open_reference_v8.step"))

    # Hard collision test against the populated KiCad model.  Zero volume means
    # the enclosure solids do not occupy component/PCB geometry.  Intentional
    # near-contact at button actuators is represented by separate caps, not shell.
    front_collision = front.val().intersect(pcb.val()).Volume()
    rear_collision = rear.val().intersect(pcb.val()).Volume()
    shell_collision = front.val().intersect(rear.val()).Volume()
    hp_clearance = make_headphone_clearance().val()
    hp_front_blockage = front.val().intersect(hp_clearance).Volume()
    hp_rear_blockage = rear.val().intersect(hp_clearance).Volume()
    speaker_egress = make_external_speaker_egress().val()
    speaker_egress_blockage = front.val().intersect(speaker_egress).Volume()
    sma_clearance = make_sma_bulkhead_clearance().val()
    sma_blockage = rear.val().intersect(sma_clearance).Volume()
    stand_closed_collision = rear.val().intersect(stand.val()).Volume()
    stand_open_collision = rear.val().intersect(stand_open.val()).Volume()
    sd_access = make_sd_access().val()
    sd_front_blockage = front.val().intersect(sd_access).Volume()
    sd_rear_blockage = rear.val().intersect(sd_access).Volume()

    # Folded antenna body clearance above the SMA hinge.  The two V8 saddles are
    # intentionally open crescents and must not clamp/intersect the 9 mm whip.
    antenna_body_clearance = cyl_y(
        ANTENNA_SIDE_STOW_Y0 + 8.0,
        ANTENNA_SIDE_STOW_Y1,
        ANTENNA_D / 2.0 + 0.20,
        ANTENNA_SIDE_STOW_X,
        ANTENNA_SIDE_STOW_Z,
    ).val()
    antenna_body_blockage = rear.val().intersect(antenna_body_clearance).Volume()

    # M2 screw shafts must have a completely open path from beneath the lens to
    # the rear insert pockets.  The PCB itself contributes the existing 2.2 mm
    # plated mounting holes at the same coordinates.
    case_screw_blockages = []
    for x, y in CASE_SCREWS:
        corridor = circular_prism(
            2.15, -0.05, FRONT_Z1 + 0.45, x, y
        ).val()
        case_screw_blockages.append(
            round(front.val().intersect(corridor).Volume()
                  + rear.val().intersect(corridor).Volume(), 6)
        )

    # V8 control QA.  Buttons deliberately have a tiny interference with the real
    # 7.2 mm actuator because their split sockets are press-fit retainers.  They
    # must, however, remain completely clear of the shell at rest and through the
    # expected tactile-switch travel.  The encoder knob gets the same axial check.
    b1_shell_rest = b1.val().intersect(front.val()).Volume()
    b2_shell_rest = b2.val().intersect(front.val()).Volume()
    knob_shell_rest = knob.val().intersect(front.val()).Volume()
    b1_pressed = b1.val().translate(cq.Vector(0.0, 0.0, -0.5))
    b2_pressed = b2.val().translate(cq.Vector(0.0, 0.0, -0.5))
    knob_pressed = knob.val().translate(cq.Vector(0.0, 0.0, -0.5))
    b1_shell_pressed = b1_pressed.intersect(front.val()).Volume()
    b2_shell_pressed = b2_pressed.intersect(front.val()).Volume()
    knob_shell_pressed = knob_pressed.intersect(front.val()).Volume()
    b1_actuator_interference = b1.val().intersect(pcb.val()).Volume()
    b2_actuator_interference = b2.val().intersect(pcb.val()).Volume()

    if hp_front_blockage > 1e-6 or hp_rear_blockage > 1e-6:
        raise RuntimeError(
            f"Headphone corridor blocked: front={hp_front_blockage:.6f} mm^3 "
            f"rear={hp_rear_blockage:.6f} mm^3"
        )
    if speaker_egress_blockage > 1e-6:
        raise RuntimeError(f"External speaker cable egress blocked: {speaker_egress_blockage:.6f} mm^3")
    if front_collision > 1e-6 or rear_collision > 1e-6:
        raise RuntimeError(
            f"Shell/PCB collision detected: front={front_collision:.6f}, "
            f"rear={rear_collision:.6f} mm^3"
        )
    if sma_blockage > 1e-6:
        raise RuntimeError(f"SMA bulkhead corridor blocked: {sma_blockage:.6f} mm^3")
    if sd_front_blockage > 1e-6 or sd_rear_blockage > 1e-6:
        raise RuntimeError(
            f"microSD access blocked: front={sd_front_blockage:.6f}, "
            f"rear={sd_rear_blockage:.6f} mm^3"
        )
    if antenna_body_blockage > 1e-6:
        raise RuntimeError(
            f"Folded antenna body intersects cradle/case: {antenna_body_blockage:.6f} mm^3"
        )
    if any(v > 1e-6 for v in case_screw_blockages):
        raise RuntimeError(f"Hidden M2 screw corridor blocked: {case_screw_blockages}")
    if shell_collision > 1e-6:
        raise RuntimeError(f"Front/rear shell collision detected: {shell_collision:.6f} mm^3")
    if stand_closed_collision > 1e-6:
        raise RuntimeError(f"Closed kickstand intersects rear shell: {stand_closed_collision:.6f} mm^3")
    if stand_open_collision > 1e-6:
        raise RuntimeError(f"Open kickstand intersects rear shell: {stand_open_collision:.6f} mm^3")
    if max(b1_shell_rest, b2_shell_rest, knob_shell_rest,
           b1_shell_pressed, b2_shell_pressed, knob_shell_pressed) > 1e-6:
        raise RuntimeError(
            "Control/shell collision detected: "
            f"B1={b1_shell_rest:.6f}/{b1_shell_pressed:.6f}, "
            f"B2={b2_shell_rest:.6f}/{b2_shell_pressed:.6f}, "
            f"ENC={knob_shell_rest:.6f}/{knob_shell_pressed:.6f} mm^3"
        )
    # The split sockets should lightly interfere with the 7.2 mm switch caps.
    # Too little means they can fall off; too much indicates an unrealistic rigid
    # collision rather than a compliant printed press fit.
    for label, interference in (("left", b1_actuator_interference),
                                ("right", b2_actuator_interference)):
        if not (0.02 <= interference <= 0.20):
            raise RuntimeError(
                f"{label} button press-fit interference out of range: "
                f"{interference:.6f} mm^3"
            )

    report["design"] = {
        "case_nominal_mm": [CASE_W, CASE_H, FRONT_Z1 - REAR_Z0],
        "rear_surface": "uniform smooth back; no battery hump",
        "rear_back_wall_mm": REAR_BACK_WALL,
        "wall_mm": WALL,
        "pcb_mm": [PCB_W, PCB_H, PCB_T],
        "speaker": {
            "internal": False,
            "external_only": EXTERNAL_SPEAKER_ONLY,
            "pcb_connector": "LS1 PicoBlade 1.25 mm",
            "cable_egress_diameter_mm": SPEAKER_EGRESS_D,
            "cable_egress_yz_mm": [SPEAKER_EGRESS_Y, SPEAKER_EGRESS_Z],
            "egress_blockage_volume_mm3": round(speaker_egress_blockage, 6),
        },
        "battery_reserve_mm": [BATTERY_W, BATTERY_H, BATTERY_T],
        "battery_capacity_mah_as_supplied": BATTERY_CAPACITY_MAH,
        "rf_keepout_xy_mm": [RF_KEEP_X0, RF_KEEP_Y0, RF_KEEP_W, RF_KEEP_H],
        "source_board_sha256": datums.get("sha256"),
        "populated_pcb_collision_volume_mm3": {
            "front_shell": round(front_collision, 6),
            "rear_shell": round(rear_collision, 6),
        },
        "front_rear_collision_volume_mm3": round(shell_collision, 6),
        "hidden_fastening": {
            "type": "two M2 screws hidden beneath removable screen lens",
            "pcb_mount_holes_reused_xy_mm": CASE_SCREWS,
            "front_clearance_diameter_mm": CASE_SCREW_CLEARANCE_D,
            "head_pocket_diameter_mm": CASE_SCREW_HEAD_D,
            "maximum_recommended_head_height_mm": 1.3,
            "rear_insert_pocket_diameter_mm": CASE_INSERT_D,
            "rear_insert_pocket_depth_mm": CASE_INSERT_DEPTH,
            "screw_corridor_blockage_mm3": case_screw_blockages,
            "assembly_note": "fit PCB and front shell, tighten two M2 screws, then fit lens to conceal screw heads",
        },
        "microsd_access": {
            "style": "narrow card tunnel with broad top-edge thumb scallop",
            "slot_width_mm": SD_SLOT_W,
            "thumb_scoop_width_mm": SD_SCOOP_W,
            "thumb_scoop_radius_mm": SD_SCOOP_R,
            "front_blockage_volume_mm3": round(sd_front_blockage, 6),
            "rear_blockage_volume_mm3": round(sd_rear_blockage, 6),
        },
        "headphone_opening": {
            "jack_ref": "J6",
            "bore_axis_xyz_mm": [HEADPHONE[0], None, 4.1],
            "connector_mouth_y_mm": -2.29,
            "overmould_diameter_mm": 11.0,
            "overmould_y_range_mm": [-6.5, -1.9],
            "shaft_clearance_diameter_mm": 6.5,
            "shaft_y_range_mm": [-2.1, 4.5],
            "front_shell_blockage_volume_mm3": round(hp_front_blockage, 6),
            "rear_shell_blockage_volume_mm3": round(hp_rear_blockage, 6),
        },
        "controls": {
            "tactile_buttons": {
                "style": "paired soft pebble caps",
                "assembly": "press on from outside after PCB/front-shell assembly",
                "switch_refs": ["SW3", "SW4"],
                "switch_actuator_mm": [7.2, 7.2],
                "split_socket_outer_mm": BUTTON_SOCKET_OUTER,
                "split_socket_nominal_inner_mm": BUTTON_SOCKET_INNER,
                "left_rotation_deg": BUTTON_LEFT_ANGLE,
                "right_rotation_deg": BUTTON_RIGHT_ANGLE,
                "shell_collision_rest_mm3": [round(b1_shell_rest, 6), round(b2_shell_rest, 6)],
                "shell_collision_at_0p5mm_press_mm3": [
                    round(b1_shell_pressed, 6), round(b2_shell_pressed, 6)
                ],
                "intentional_actuator_interference_mm3": [
                    round(b1_actuator_interference, 6),
                    round(b2_actuator_interference, 6),
                ],
            },
            "encoder": {
                "ref": "SW2",
                "part": "PEC11L-4120F-S0020",
                "shaft": "6.0 mm 18-tooth knurled",
                "retention": "blind split-collet press fit",
                "nominal_bore_mm": ENCODER_BORE_D,
                "lead_in_mm": ENCODER_LEADIN_D,
                "shaft_end_clearance_mm": 0.5,
                "optional_shell_support": "M7x0.75 encoder bushing/nut hidden under knob",
                "nut_recess_diameter_mm": ENCODER_NUT_RECESS_D,
                "nut_recess_depth_mm": ENCODER_NUT_RECESS_DEPTH,
                "shell_collision_rest_mm3": round(knob_shell_rest, 6),
                "shell_collision_at_0p5mm_press_mm3": round(knob_shell_pressed, 6),
            },
        },
        "qon_sw1_external_actuator": False,
        "kickstand": {
            "type": "recessed multimeter-style rear frame",
            "outer_mm": [STAND_W, STAND_H, STAND_T],
            "recess_mm": [STAND_RECESS_W, STAND_RECESS_H, STAND_RECESS_DEPTH],
            "hinge_pin_diameter_mm": STAND_PIN_D,
            "suggested_pin": "1.75 mm filament or similar smooth pin",
            "nominal_open_angle_deg": STAND_OPEN_ANGLE_DEG,
            "closed_shell_collision_volume_mm3": round(stand_closed_collision, 6),
            "open_shell_collision_volume_mm3": round(stand_open_collision, 6),
        },
        "fm_external_antenna": {
            "board_connector": "J3 U.FL",
            "ufl_xy_mm": [FM_UFL[0], FM_UFL[1]],
            "pigtail": "RF1.13 U.FL/IPX to SMA bulkhead",
            "pigtail_nominal_length_mm": FM_COAX_LENGTH,
            "reserved_route_length_mm": round(coax_route_length(), 2),
            "minimum_design_bend_radius_mm": FM_COAX_BEND_R,
            "sma_bulkhead_side": "left",
            "sma_bulkhead_yz_mm": [SMA_CENTER_Y, SMA_CENTER_Z],
            "sma_mount_hole_diameter_mm": SMA_HOLE_D,
            "sma_reinforcement_boss_diameter_mm": SMA_BOSS_D,
            "field_rework_note": "Boss has enough material for cautious enlargement if the delivered antenna is not the SMA variant shown by the listing title",
            "sma_corridor_blockage_volume_mm3": round(sma_blockage, 6),
            "antenna_collapsed_mm": ANTENNA_COLLAPSED,
            "antenna_extended_mm": ANTENNA_EXTENDED,
            "antenna_body_diameter_mm": ANTENNA_D,
            "stow": "hinged upward along left side into two soft nesting saddles; slight diagonal cant available if required",
            "reserved_side_stow_y_mm": [ANTENNA_SIDE_STOW_Y0, ANTENNA_SIDE_STOW_Y1],
            "cradle_centres_y_mm": list(ANTENNA_CRADLE_YS),
            "cradle_style": "open crescent saddles; guide only, no brittle snap fit",
            "folded_body_blockage_volume_mm3": round(antenna_body_blockage, 6),
        },
    }

    # Remove obsolete artefacts from revisions that exposed SW1/QON externally.
    for obsolete in (OUT / "kiku_power_button_v1.step", OUT / "kiku_power_button_v1.stl"):
        obsolete.unlink(missing_ok=True)
    (REVIEW / "kiku_housing_v8_validation.json").write_text(json.dumps(report, indent=2))

    print("Kiku housing v8 exported")
    for name, info in report.items():
        if name != "design":
            print(f"  {name}: valid={info['valid']} volume={info['volume_mm3']:.1f} mm^3 bbox={info['bbox_mm']}")


if __name__ == "__main__":
    main()
