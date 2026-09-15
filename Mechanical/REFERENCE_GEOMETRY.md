# Mechanical reference geometry

The kiku enclosure is project source, but a complete mechanical fit review also
uses component 3D models referenced by the KiCad PCB. Those component models do
not all have a single documented redistribution licence, so the public source
tree deliberately separates **project-owned outputs** from **local fit-check
inputs**.

## What is public

The public tree includes:

- `Walkman - Blobject.kicad_pcb`, the electrical/mechanical board source of truth;
- `reference/board_datums.json`, generated from that board by
  `scripts/extract_board.py`;
- `reference/pcb_bare.step`, which contains project board geometry without the
  populated component-model bundle;
- `reference/component_envelopes.json`, numeric provenance/fidelity and bounding
  metadata from the last reviewed reference set;
- the project-owned enclosure/part STEP and STL outputs;
- machine-readable clearance/interface results and project-generated review
  material.

The currently reviewed board datums contain 133 footprint-model references: 112
to the KiCad 9 model library and 21 to the local `3dmodels/` directory. The
source-audit command below derives its result from `board_datums.json`, so a board
change is not hidden by these documentation counts.

The public tree does **not** include:

- the local `3dmodels/` component bundle;
- transformed/frozen `REF_*.brep` component solids;
- `reference/pcb_populated.step`;
- P0/P1 fit-check assembly STEP files that embed those component solids;
- V-series `kiku_fit_check_with_pcb_v*.step` outputs for the same reason.

These exclusions are intentional. Converting or transforming a third-party model
to BREP/STEP does not make it a project-owned asset.

## Obtaining the local inputs

`reference/board_datums.json` records the exact KiCad model expression for every
footprint. There are two input classes:

1. `${KICAD9_3DMODEL_DIR}/...` models. Install the matching KiCad 9 3D-model
   libraries and use them under their published licence terms.
2. `${KIPRJMOD}/3dmodels/...` models. Obtain the applicable model from the
   original manufacturer, KiCad/component-library or other recorded publisher
   under that source's terms, then place it under `3dmodels/` using the filename
   recorded by the board. The public repository does not provide or relicence it.

The public metadata records model expressions, transforms and the reviewed final
bounding boxes, but it does not invent missing upstream URLs, licences or model
revision hashes. For an exact repeat of the historical reference QA, use the same
upstream model revision and compare the regenerated `component_envelopes.json`
against the checked-in reviewed metadata. A different model that merely has the
same filename must not be assumed equivalent.

The D3 reference is the documented exception: the reviewed model intentionally
uses a conservative synthetic allowance when its KiCad 0201 STEP is unavailable.
That fallback is project-generated and is labelled `UNVERIFIED` in
`component_envelopes.json`.

If KiCad is installed somewhere other than the default Windows KiCad 9 path, set
`KICAD9_3DMODEL_DIR` to its `3dmodels` directory before running the reference
tools.

The checked-in bare board STEP can be regenerated without component models. With
KiCad 9, the reviewed command is:

```powershell
kicad-cli pcb export step --board-only --force `
  --user-origin 72.11x157.90mm `
  -o Mechanical/reference/pcb_bare.step `
  "Walkman - Blobject.kicad_pcb"
```

The explicit user origin maps the KiCad board outline to the mechanical
coordinate system (X = 0..60 mm, Y = 0..105 mm). KiCad STEP serialisation is not
byte-stable across exports, so validate geometry rather than file SHA alone. The
reviewed KiCad 9.0.2 export and the checked-in STEP both report one solid with
volume 9420.049197045 mm^3 and bounds `[0, 0, 0, 60, 105, 1.51]` mm.

From the repository root, audit the local model set with:

```powershell
python Mechanical/scripts/reference_geometry.py --check-sources
```

The command fails if any exact source required by the reviewed reference set is
missing, other than the documented D3 allowance. It prints the unresolved board
model expression so the missing file is obtained from its original publisher
rather than copied from this repository.

After the source audit passes, an optional local transformed cache can be made
with:

```powershell
python Mechanical/scripts/reference_geometry.py --freeze-local
```

This writes transformed component BREPs under `Mechanical/reference/frozen/`.
They are ignored by Git and must not be added to a public package. Their purpose
is only to make subsequent local CAD iterations independent of machine-specific
library paths.

## Rebuilding and validating

For the P0/P1 Mame engineering revisions, a full strict reference rebuild uses:

```powershell
python Mechanical/scripts/reference_geometry.py --check-sources
python Mechanical/scripts/housing.py
python Mechanical/scripts/verify.py
python Mechanical/scripts/interface_review_p0.py
python Mechanical/scripts/antenna_p1.py
python Mechanical/scripts/verify_p1.py
python Mechanical/scripts/check_antenna_p1.py
```

Those commands may locally generate populated/frozen fit-check geometry. The
geometry is validation input/output, not redistributable project source.

The public packaging commands are intentionally different:

```powershell
python Mechanical/scripts/package_p0.py
python Mechanical/scripts/package_p1.py
```

They consume the checked-in project-owned exports and recorded QA reports, and
do not require or package third-party model solids. This keeps the public package
reproducible from a stripped public checkout without weakening the previously
performed strict hardware-reference checks.

For the newer V-series housing generator, `scripts/build_housing.py` performs
collision tests against a locally generated `reference/pcb_populated.step` and
may generate `kiku_fit_check_with_pcb_v*.step` review assemblies. Those fit-check
STEPs are local-only because they embed the populated reference. A fresh full V8
fit validation therefore also requires the same local third-party model setup.
Do not substitute the bare PCB for those component collision checks or interpret
a housing-only build as equivalent validation.
