"""Prepare Kiku V8 smooth-back/blobject-control CAD for a 3D-printing service.

Creates two deterministic ZIP packages under Mechanical/upload:
  - Kiku_Housing_V8_PRINT_UPLOAD.zip: STL files + upload notes
  - Kiku_Housing_V8_ENGINEERING.zip: STEP/source/validation/review/reference assets
"""

from __future__ import annotations

from pathlib import Path
import csv
import hashlib
import json
import shutil
import struct
import zipfile


ROOT = Path(__file__).resolve().parents[2]
MECH = ROOT / "Mechanical"
EXP = MECH / "exports"
REV = MECH / "review"
UPLOAD = MECH / "upload"
STAGE_PRINT = UPLOAD / "Kiku_Housing_V8_PRINT_UPLOAD"
STAGE_ENG = UPLOAD / "Kiku_Housing_V8_ENGINEERING"


PRINT_FILES = {
    "01_Kiku_Front_Shell_External_Speaker_V8.stl": EXP / "kiku_front_shell_external_speaker_v8.stl",
    "02_Kiku_Rear_Shell_Smooth_Stand_V8.stl": EXP / "kiku_rear_shell_smooth_stand_v8.stl",
    "03_Kiku_Kickstand_V8.stl": EXP / "kiku_kickstand_v8.stl",
    "04_Kiku_Button_Left_V8.stl": EXP / "kiku_button_left_v8.stl",
    "05_Kiku_Button_Right_V8.stl": EXP / "kiku_button_right_v8.stl",
    "06_Kiku_Encoder_Knob_V8.stl": EXP / "kiku_encoder_knob_v8.stl",
    "07_Kiku_Screen_Lens_V8.stl": EXP / "kiku_screen_lens_v8.stl",
}


ENGINEERING_FILES = {
    "STEP/Kiku_Front_Shell_External_Speaker_V8.step": EXP / "kiku_front_shell_external_speaker_v8.step",
    "STEP/Kiku_Rear_Shell_Smooth_Stand_V8.step": EXP / "kiku_rear_shell_smooth_stand_v8.step",
    "STEP/Kiku_Kickstand_V8.step": EXP / "kiku_kickstand_v8.step",
    "STEP/Kiku_Screen_Lens_V8.step": EXP / "kiku_screen_lens_v8.step",
    "STEP/Kiku_Button_Left_V8.step": EXP / "kiku_button_left_v8.step",
    "STEP/Kiku_Button_Right_V8.step": EXP / "kiku_button_right_v8.step",
    "STEP/Kiku_Encoder_Knob_V8.step": EXP / "kiku_encoder_knob_v8.step",
    "STEP/Kiku_Housing_Assembly_V8.step": EXP / "kiku_housing_assembly_v8.step",
    "STEP/Kiku_Fit_Check_With_PCB_V8.step": EXP / "kiku_fit_check_with_pcb_v8.step",
    "STEP/Kiku_Packaging_Keepouts_V8.step": EXP / "kiku_packaging_keepouts_v8.step",
    "STEP/Kiku_Kickstand_Open_Reference_V8.step": EXP / "kiku_kickstand_open_reference_v8.step",
    "SOURCE/build_housing.py": MECH / "scripts" / "build_housing.py",
    "SOURCE/render_review.py": MECH / "scripts" / "render_review.py",
    "SOURCE/prepare_upload.py": MECH / "scripts" / "prepare_upload.py",
    "SOURCE/README.md": MECH / "README.md",
    "VALIDATION/kiku_housing_v8_validation.json": REV / "kiku_housing_v8_validation.json",
    "REVIEW/kiku_housing_v8_front_3q.png": REV / "kiku_housing_v8_front_3q.png",
    "REVIEW/kiku_housing_v8_rear_3q.png": REV / "kiku_housing_v8_rear_3q.png",
    "REVIEW/kiku_housing_v8_exploded.png": REV / "kiku_housing_v8_exploded.png",
    "REVIEW/kiku_housing_v8_stand_open.png": REV / "kiku_housing_v8_stand_open.png",
    "REVIEW/kiku_housing_v8_controls_closeup.png": REV / "kiku_housing_v8_controls_closeup.png",
    "REFERENCE/FM_ANTENNA_REFERENCE.md": MECH / "reference" / "ali_antenna" / "README.md",
}

OPTIONAL_ENGINEERING_FILES = {
    "REFERENCE/telescopic_antenna_dimensions.jpg": MECH / "reference" / "ali_antenna" / "img_05.jpg",
    "REFERENCE/ufl_sma_pigtail.jpg": MECH / "reference" / "ali_antenna" / "img_08.jpg",
}


UPLOAD_NOTES = """KIKU HOUSING V8 - BLOBOBJECT REFINEMENT / HIDDEN FASTENERS

Units: millimetres (mm)
Status: first physical-fit prototype, not injection-mould release CAD

SPEAKER
  No internal speaker is fitted in V8.
  LS1 remains the electrical speaker output.
  A 4.4 mm right-side cable egress is provided for an externally mounted speaker lead.

BATTERY
  10 x 34 x 50 mm LiPo envelope, 200 mAh as supplied by the project owner.
  V8 removes the old local battery hump by making the entire rear shell uniformly deeper.

RECOMMENDED PROTOTYPE PROCESS
  Main front/rear shells: PA12 SLS/MJF preferred; resin/FDM also suitable for fit checks.
  Buttons/knob: PA12 is preferred because the hidden split sockets/collet are compliant features.
  For FDM, print one control set first and tune socket/bore dimensions by +/-0.10 mm if required.
  Screen lens: upload separately if using clear/translucent resin; otherwise fabricate in clear sheet/acrylic.

CONTROLS / ASSEMBLY
  SW3/SW4 button caps are soft pebble forms, rotated -7 and +7 degrees for a paired blobject look.
  Each button has a hidden four-way split socket for the real 7.2 x 7.2 mm switch actuator.
  Nominal socket throat: 7.05 mm with a wider lead-in. This is an intentional compliant press fit.
  Assembly: install PCB/front shell first, then press each cap onto its tactile actuator from outside.
  No glue or loose centre post is required.
  PEC11L encoder shaft: 6.0 mm, 18-tooth knurled.
  Encoder knob uses a blind 5.75 mm split-collet bore with 6.35 mm lead-in.
  The front shell includes a hidden 12.4 mm recess for an optional M7x0.75 bushing nut/washer.
  Fit the encoder nut first if used, then press the knob onto the shaft.
  Shell clearances have been checked through 0.5 mm of button/encoder push travel.

FM ANTENNA
  PCB J3 U.FL -> ordered 100 mm RF1.13 U.FL/IPX-to-SMA bulkhead pigtail.
  SMA bulkhead mounts on the LEFT side of the rear shell.
  Ordered telescopic antenna listing: 105 mm collapsed, 310 mm extended, 9 mm diameter.
  Hinge lets the rod stow upward along the left side rather than adding thickness to the back.
  Two open crescent saddles nest the folded rod against the case without a brittle snap fit.
  The internal cable route is approximately 88.3 mm, leaving service slack in the 100 mm lead.

HIDDEN CASE FASTENING
  Two M2 screws reuse the PCB upper mounting holes at X=6/54, Y=99 mm.
  Their heads are concealed under the removable screen lens.
  Use low-profile/button-head M2 screws with <=1.3 mm head height.
  Rear posts include 3.25 mm x 3.6 mm insert pockets for M2 brass inserts / fit trials.

MICROSD
  The rectangular top opening is replaced by a narrower card tunnel and broad thumb scallop.

BODY / STAND
  Nominal body thickness: 28.7 mm.
  Rear surface is smooth and uniform; there is no local battery hump.
  Rear back wall is 3.0 mm nominal before the kickstand recess.
  Recessed multimeter-style stand: 42 x 56 x 1.4 mm frame.
  Stand hinge: nominal 1.9 mm bore intended for ~1.75 mm filament or similar smooth pin.
  Nominal open reference angle: 62 degrees.

FILES TO UPLOAD
  01_Kiku_Front_Shell_External_Speaker_V8.stl   qty 1
  02_Kiku_Rear_Shell_Smooth_Stand_V8.stl        qty 1
  03_Kiku_Kickstand_V8.stl                      qty 1
  04_Kiku_Button_Left_V8.stl                    qty 1
  05_Kiku_Button_Right_V8.stl                   qty 1
  06_Kiku_Encoder_Knob_V8.stl                   qty 1
  07_Kiku_Screen_Lens_V8.stl                    qty 1 (clear process only)

FIT CHECKS REQUIRED AFTER FIRST PRINT
  - PCB mounting/post fit
  - USB-C and headphone plug seating
  - microSD insertion/removal
  - hidden M2 case screws tighten correctly before the screen lens is fitted
  - lens clears the selected low-profile screw heads
  - pebble button press-fit: secure on SW3/SW4 but still removable without damaging the switch
  - button caps move through full tactile travel without rubbing their shallow front recesses
  - encoder split-collet grip on the 6.0 mm knurled shaft
  - optional M7x0.75 encoder nut/washer fits under the knob without limiting push-to-select travel
  - SW1/QON remains internal; there is intentionally no external QON button
  - LiPo cable exit and swelling clearance
  - external speaker lead exits freely through the 4.4 mm right-side egress
  - kickstand opens/closes without scraping and has useful hinge friction
  - tune/drill the 1.9 mm hinge bore if required for the chosen pin/process
  - SMA bulkhead/nut fit in the 6.8 mm side hole
  - U.FL/coax bend radius, service slack and BM83 RF keepout
  - telescopic antenna hinge clears the case and folds along the left side
"""


def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1024 * 1024), b""):
            h.update(chunk)
    return h.hexdigest()


def stl_triangle_count(path: Path) -> tuple[str, int]:
    """Return STL encoding and triangle count; accepts binary or ASCII STL."""
    size = path.stat().st_size
    with path.open("rb") as fh:
        header = fh.read(84)
    if len(header) >= 84:
        tri = struct.unpack("<I", header[80:84])[0]
        if 84 + tri * 50 == size:
            return "binary", tri
    text = path.read_text(errors="ignore")
    return "ascii", text.lower().count("facet normal")


def clean_stage(path: Path) -> None:
    if path.exists():
        shutil.rmtree(path)
    path.mkdir(parents=True, exist_ok=True)


def copy_map(mapping: dict[str, Path], stage: Path) -> None:
    for relative, source in mapping.items():
        if not source.is_file():
            raise FileNotFoundError(source)
        dest = stage / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)


def copy_optional_map(mapping: dict[str, Path], stage: Path) -> None:
    """Copy local third-party references when available without requiring them."""
    for relative, source in mapping.items():
        if not source.is_file():
            continue
        dest = stage / relative
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, dest)


def write_manifest(stage: Path, file_names: list[str]) -> None:
    rows = []
    for name in file_names:
        path = stage / name
        enc, tris = stl_triangle_count(path)
        rows.append({
            "file": name,
            "bytes": path.stat().st_size,
            "stl_encoding": enc,
            "triangles": tris,
            "sha256": sha256(path),
        })
    with (stage / "UPLOAD_MANIFEST.csv").open("w", newline="", encoding="utf-8") as fh:
        writer = csv.DictWriter(fh, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)


def make_zip(stage: Path, zip_path: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in sorted(stage.rglob("*")):
            if path.is_file():
                zf.write(path, path.relative_to(stage))


def main() -> None:
    UPLOAD.mkdir(parents=True, exist_ok=True)

    clean_stage(STAGE_PRINT)
    copy_map(PRINT_FILES, STAGE_PRINT)
    (STAGE_PRINT / "READ_ME_BEFORE_UPLOAD.txt").write_text(UPLOAD_NOTES, encoding="utf-8")
    write_manifest(STAGE_PRINT, list(PRINT_FILES))

    validation = json.loads((REV / "kiku_housing_v8_validation.json").read_text())
    design = validation["design"]
    if design["populated_pcb_collision_volume_mm3"]["front_shell"] != 0.0:
        raise RuntimeError("Front-shell PCB collision detected")
    if design["populated_pcb_collision_volume_mm3"]["rear_shell"] != 0.0:
        raise RuntimeError("Rear-shell PCB collision detected")
    if design["front_rear_collision_volume_mm3"] != 0.0:
        raise RuntimeError("Front/rear shell collision detected")
    if not design["speaker"].get("external_only", False):
        raise RuntimeError("V8 must remain external-speaker-only")
    if design["speaker"]["egress_blockage_volume_mm3"] != 0.0:
        raise RuntimeError("External speaker cable egress is blocked")
    if design["headphone_opening"]["front_shell_blockage_volume_mm3"] != 0.0:
        raise RuntimeError("Headphone corridor blocked by front shell")
    if design["headphone_opening"]["rear_shell_blockage_volume_mm3"] != 0.0:
        raise RuntimeError("Headphone corridor blocked by rear shell")
    if design["fm_external_antenna"]["sma_corridor_blockage_volume_mm3"] != 0.0:
        raise RuntimeError("FM SMA bulkhead corridor blocked")
    if design["qon_sw1_external_actuator"]:
        raise RuntimeError("SW1/QON must remain internal-only")
    if design["kickstand"]["closed_shell_collision_volume_mm3"] != 0.0:
        raise RuntimeError("Closed kickstand intersects rear shell")
    if design["kickstand"]["open_shell_collision_volume_mm3"] != 0.0:
        raise RuntimeError("Open kickstand intersects rear shell")
    if any(v != 0.0 for v in design["hidden_fastening"]["screw_corridor_blockage_mm3"]):
        raise RuntimeError("Hidden M2 screw corridor is blocked")
    if design["microsd_access"]["front_blockage_volume_mm3"] != 0.0 or design["microsd_access"]["rear_blockage_volume_mm3"] != 0.0:
        raise RuntimeError("microSD access is blocked")
    if design["fm_external_antenna"]["folded_body_blockage_volume_mm3"] != 0.0:
        raise RuntimeError("Folded antenna collides with cradle/case")
    controls = design["controls"]
    if any(v != 0.0 for v in controls["tactile_buttons"]["shell_collision_rest_mm3"]):
        raise RuntimeError("Button cap intersects shell at rest")
    if any(v != 0.0 for v in controls["tactile_buttons"]["shell_collision_at_0p5mm_press_mm3"]):
        raise RuntimeError("Button cap intersects shell during 0.5 mm press check")
    if controls["encoder"]["shell_collision_rest_mm3"] != 0.0:
        raise RuntimeError("Encoder knob intersects shell at rest")
    if controls["encoder"]["shell_collision_at_0p5mm_press_mm3"] != 0.0:
        raise RuntimeError("Encoder knob intersects shell during 0.5 mm press check")
    for interference in controls["tactile_buttons"]["intentional_actuator_interference_mm3"]:
        if not (0.02 <= interference <= 0.20):
            raise RuntimeError("Button actuator press-fit interference is outside the release window")

    clean_stage(STAGE_ENG)
    copy_map(ENGINEERING_FILES, STAGE_ENG)
    copy_optional_map(OPTIONAL_ENGINEERING_FILES, STAGE_ENG)
    (STAGE_ENG / "PRINT_UPLOAD_README.txt").write_text(UPLOAD_NOTES, encoding="utf-8")

    print_zip = UPLOAD / "Kiku_Housing_V8_PRINT_UPLOAD.zip"
    eng_zip = UPLOAD / "Kiku_Housing_V8_ENGINEERING.zip"
    make_zip(STAGE_PRINT, print_zip)
    make_zip(STAGE_ENG, eng_zip)

    print(f"READY: {print_zip}")
    print(f"READY: {eng_zip}")
    print("Validation gate: PASS (0 shell/PCB and 0 shell/shell intersection volume)")


if __name__ == "__main__":
    main()
