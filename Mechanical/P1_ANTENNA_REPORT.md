# Kiku / Mame P1 — FM antenna integration

P1 corrects the antenna omission in the delivered Mame P0 housing. It adds the external fitting, folding space and a complete internal route for the user's **100 mm U.FL-to-standard-SMA-female pigtail**. Use the P1 front and rear shells together. P0 files and the separate V-series housing are not interchangeable with this revision.

This is a prototype engineering release. The purchased pigtail variant is confirmed; the listing does not provide a full fitting/hinge mechanical drawing. The corresponding parameters and reference bodies are explicitly marked unconfirmed rather than presented as exact manufacturer CAD.

## Hardware and evidence

| Item | Confirmed basis | Detail still to check on arrival |
|---|---|---|
| [Pigtail, AliExpress 1005009812335103](https://www.aliexpress.com/item/1005009812335103.html) | User confirms 10 cm, U.FL to standard SMA female. Listing identifies RF1.13 cable. | Usable cable length between terminations, bulkhead thread length/flats, nut/washer sizes, plug height and supplier minimum bend radius. |
| [Telescopic antenna, AliExpress 1005007806214529](https://www.aliexpress.com/item/1005007806214529.html) | Listing dimension image: 105 mm collapsed overall, 310 mm extended overall, 9 mm diameter; hinged base. | Hinge centre relative to coupling face and separate rod length. Photo shows a centre pin; confirm actual mating hardware, as listing connector wording is inconsistent. |
| J3 / FM_ANT | Existing KiCad footprint at mechanical XY 57.395, 66.550. Supplied STEP cylindrical mating axis is **58.155, 66.550**. | Actual mated U.FL plug geometry. PCB and J3 placement are unchanged. |

Cached supplier images are intentionally excluded from the public source tree;
the source product links above remain the reproducible reference. The electrical
listing's broad frequency marketing claim is not used as an antenna-performance guarantee.

## Mount, stowage and load path

The SMA fitting exits the **upper left side at Y = 96 mm, Z = -9 mm**, with its outer mounting face at X = -10.5 mm. This keeps the lead short and puts the large external metal assembly far from the BM83 antenna at the lower right.

The rear shell has a 12.8 mm diameter reinforced mounting pod, a **6.8 mm through-hole**, a 0.35 mm entrance lead-in, and an internal 10 mm diameter fitting recess. The resulting flat clamping land is 3.0 mm thick. The hole assumes the standard nominal 6.35 mm SMA thread; nut and washer bodies are conservative circular envelopes. Confirm the available threaded length accommodates the land, washer and full nut engagement. The circular hole currently relies on the tightened washer/nut for rotational restraint; a keyed flat must follow the delivered fitting's actual dimensions.

The intended structural path is antenna → bulkhead fitting → washer/nut and reinforced rear shell. Antenna torque must not be reacted by twisting the coax or pulling J3. Tighten the bulkhead while holding its body, then connect the U.FL lead. Pull-out, torque and drop strength remain physical test items.

The antenna folds downward along the left side into two **open outward-facing saddles at Y = 45 and 72 mm**. Saddle clearance radius is 4.85 mm, giving 0.35 mm nominal radial allowance around the 9 mm antenna envelope. These are resting guides, not locking clips. Lift/swing the antenna outward to deploy it; remove the detachable antenna for the smallest pocket envelope.

The hinge offset is an explicit **12 mm provisional parameter** measured outward from the panel face. Because the supplier gives only overall lengths, the full 105 mm is conservatively reserved as a rod extending from that provisional pivot; it is not claimed to be the actual rod length. The conservative folded envelope may project slightly below the housing. The 310 mm extended envelope is supplied in a separate STEP assembly. Adjust the hinge offset and saddles after measuring the hardware rather than bending the antenna to suit the print.

The resulting **reserved folded envelope is approximately 94.6 x 122.2 x 46.8 mm** (width x height x depth including the knob). The manufactured housing including empty side saddles is approximately 90.1 x 120.9 x 46.8 mm. These are mesh/envelope dimensions, not a promise of the exact purchased-antenna outline; see `review/P1_envelope_dimensions.json`. Grey antenna bodies in the review images are conservative clearance representations. The public package records the extended-envelope dimensions rather than redistributing a fit-check assembly that embeds third-party component geometry.

## Internal cable route

The route starts on the actual J3 mating axis, exits toward the right, bends around the fixed PCB edge, and crosses the upper board back above the battery guard to reach the left bulkhead. It avoids the battery pouch, the speaker and the lower-right Bluetooth antenna region. The original P0 dummy coax and unused tall guides are removed.

The centreline uses straight segments joined by tangent **4 mm radius circular bends**, with a 1.13 mm cable envelope and 0.4 mm additional radial clearance. Both shells are relieved where needed; two supported guide locations follow the route. These guides control placement but are not a qualified tensile strain-relief clamp. The PCB must be lowered with the wire seated and visible, never used to force a loose wire into place.

The generated centreline is **86.78 mm**, leaving **13.22 mm** within the nominal 100 mm lead. `exports/P1/antenna_geometry.json` records the exact path and guide positions. That remainder is a length budget for termination differences and service movement, not guaranteed free slack: confirm how the seller measures the cable and the real minimum bend radius. If the actual cable cannot follow the route freely, change the route or lead length; do not load the U.FL connector to close the shells.

## Checks and limits

`review/clearance_review_P1.json` checks the revised shells against hardware references, printed parts and explicit corridors. It includes seven collapsed-antenna poses from 0 to 90 degrees at 15-degree intervals and the straight outward extended envelope. These are discrete pose checks, not a certified continuous three-dimensional hinge sweep.

`review/antenna_check_P1.json` separately checks the new cable/fitting envelopes against the PCB, display, battery, retainers, screws and other adjacent hardware. Cable connection at its own end fittings is intentional and excluded. This avoids treating an empty housing-only cable corridor as proof that the wire clears the electronics.

The RF review uses conservative distances from the supplied BM83 trace bounds to the added metal envelopes, including cable shielding and deployed antenna poses. The minimum for the newly added antenna hardware is **48.17 mm**; unchanged original fasteners retain their P0 separation values (minimum 18.32 mm). [Microchip recommends at least 15 mm from the onboard trace antenna to external metal for best range](https://onlinedocs.microchip.com/oxy/GUID-414904F5-364E-4377-B959-9226AD29D6A9-en-US-7/GUID-495EA464-310A-41EC-A892-5E46727B10FC.html). Geometric spacing does not replace Bluetooth/FM tests with the antenna folded, extended and held in the hand.

Other P0 validation limits remain, including print fit, display FPC routing, switch stroke/return, purchased inserts, battery leads, acoustics and manufacturing-process qualification. P1 adds actual antenna accommodation; it is not a tooling release.

## P1 verification result

The final reviewed exports report no collisions in the screened assembly pairs, all 28 explicit corridor/pose-to-shell checks are clear, and the 57 potentially adjacent cable/hardware pairs have zero overlap. All eight STL files are watertight, consistently wound and have one connected body. The new antenna hardware passes the conservative 15 mm RF spacing check. These results apply to the modelled nominal envelopes and the documented check methods; the unresolved physical fitting and performance items above remain open.

## Assembly and editable files

1. Verify the antenna and SMA female pigtail mate correctly without an adapter. Measure fitting thread length, flange/washer, hinge offset and cable length convention.
2. Install the SMA bulkhead in the rear shell's flat land. Hold its body while tightening the washer/nut; do not twist the coax.
3. Fit the speaker, battery and guard as described in the P0 report. Lay the coax along the P1 guide path above the guard, keeping its bends smooth.
4. Mate U.FL vertically onto J3 with the PCB accessible. Set the PCB into the front shell and check the cable crosses its right edge freely. Support the shells during service rather than suspending one from the pigtail. The short lead limits opening distance: release it from the open guides and disconnect U.FL before fully separating the shells. Use an assembly nest for the front and support the rear close to it while mating the lead.
5. Close the shells while observing the lead and screw locations. Check return/rotation of the controls, port/card access and absence of cable pinching.
6. Fit the antenna, confirm its hinge plane aligns with the saddles, then test folded, released and extended positions. Verify retention and radio performance before normal use.

The native model is `scripts/antenna_p1.py`, which extends `scripts/housing.py`; its antenna dimensions are in `antenna-P1.json`, with base dimensions in `parameters.json`. Run with the supplied requirements in a Python 3.12 environment:

```powershell
python Mechanical/scripts/antenna_p1.py
python Mechanical/scripts/verify_p1.py
python Mechanical/scripts/check_antenna_p1.py
python Mechanical/scripts/render_p1.py
```

The `--cached-base` option is an iteration shortcut requiring a local P0 BREP
inventory. The normal rebuild uses the editable base model. A full strict
hardware-reference rebuild requires the locally obtained component models
documented in `REFERENCE_GEOMETRY.md`; frozen transformed references and
fit-check assemblies are not redistributed. The public package contains the
project-owned housing STEP, individual parts/STLs and recorded QA results; all
STL units are mm and coordinates are assembled positions.
