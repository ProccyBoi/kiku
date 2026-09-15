"""Create a self-contained P1 antenna revision package after successful checks."""
import pathlib,json,zipfile,hashlib
R=pathlib.Path(__file__).resolve().parents[1];E=R/'exports/P1'
inv=json.loads((E/'part_inventory.json').read_text())
q=json.loads((R/'review/clearance_review_P1.json').read_text());a=json.loads((R/'review/antenna_check_P1.json').read_text())
assert all(s['valid'] for s in q['solids'].values())
assert all(s['watertight'] and s['winding_consistent'] and s['bodies']==1 for s in q['stl'].values())
assert not q['printed_part_pairs']
assert not [s for s in q['shell_to_reference']+q['corridors'] if s['status']=='COLLISION']
assert not [s for s in a['cable_to_hardware']+a['new_fittings_to_hardware'] if s['status']=='COLLISION']
assert a['rf_geometric_status']=='PASS'
files=[]
for p in E.iterdir():
 if (p.stem in inv and not p.stem.startswith('REF_')) or p.stem.startswith('CHECK_') or p.name in ('Kiku_P1_housing.step','part_inventory.json','antenna_geometry.json'):
  if p.suffix in ('.brep','.step','.stl','.json'):files.append((p,'Mechanical/exports/P1/'+p.name))
for name in ('antenna_p1.py','housing.py','reference_geometry.py','verify_p1.py','check_antenna_p1.py','render_p1.py','package_p1.py'):
 files.append((R/'scripts'/name,'Mechanical/scripts/'+name))
for name in ('antenna-P1.json','parameters.json','requirements-P0.txt','P1_ANTENNA_REPORT.md','P0_DESIGN_REPORT.md'):
 files.append((R/name,'Mechanical/'+name))
for p in (R/'review').glob('P1_*.png'):files.append((p,'Mechanical/review/'+p.name))
for name in ('clearance_review_P1.json','antenna_check_P1.json','P1_envelope_dimensions.json'):
 files.append((R/'review'/name,'Mechanical/review/'+name))
for name in ('board_datums.json','component_envelopes.json','pcb_bare.step'):
 files.append((R/'reference'/name,'Mechanical/reference/'+name))
files.append((R/'REFERENCE_GEOMETRY.md','Mechanical/REFERENCE_GEOMETRY.md'))
files.append((R/'concepts/three_concepts.png','Mechanical/concepts/three_concepts.png'))
for src,arc in ((R.parent/'LICENSE','LICENSE'),(R.parent/'LICENSES/CERN-OHL-P-2.0.txt','LICENSES/CERN-OHL-P-2.0.txt'),(R.parent/'LICENSES/MIT.txt','LICENSES/MIT.txt')):
 files.append((src,arc))
readme='''KIKU / MAME P1 — FM ANTENNA REVISION

Read Mechanical/P1_ANTENNA_REPORT.md first. It explains what changed, the confirmed
100 mm U.FL-to-standard-SMA-female pigtail, listing antenna dimensions, and the
fitting/hinge dimensions that still require measurement. The P0 report provides
background for unchanged parts; its original antenna arrangement is superseded.

P1 models:
  Mechanical/exports/P1/Kiku_P1_housing.step — manufactured/project-owned parts
  Mechanical/exports/P1/*.stl — eight current prototype printing files, mm

The local folded/extended fit-check assemblies are intentionally not included:
they embed third-party component-reference solids. Antenna dimensions and the
reviewed clearance results remain in antenna_geometry.json and the QA reports.

Use both P1 shells. Do not combine P0 or the separate V-series shell with P1.
CAD antenna bodies are dimensional clearance envelopes, not exact purchased CAD.
The 105/310 mm overall lengths are conservatively reserved beyond the hinge.

Review images:
  Mechanical/review/P1_assembled_iso.png
  Mechanical/review/P1_antenna_route.png
  Mechanical/review/P1_exploded.png
  Mechanical/review/P1_section_*.png

Editable source:
Install Mechanical/requirements-P0.txt in a Python 3.12 virtual environment.
Edit antenna-P1.json for antenna parameters; parameters.json controls the base.
Mechanical/REFERENCE_GEOMETRY.md explains how to obtain the local component
models under their original terms before a full strict hardware-reference rebuild.
The --cached-base shortcut requires a separate local P0 BREP inventory.
See LICENSE for the mixed-licence scope and LICENSES/ for the full licence texts.

This is a prototype engineering package. Physical fitting, cable bend/length,
retention strength, radio performance and the other report gates remain open.
'''
out=R/'Kiku_Mame_P1_antenna_package.zip'
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 z.writestr('START_HERE.txt',readme)
 z.writestr('SHA256_manifest.json',json.dumps({arc:hashlib.sha256(p.read_bytes()).hexdigest() for p,arc in files},indent=2))
 for p,arc in files:z.write(p,arc)
with zipfile.ZipFile(out) as z:assert z.testzip() is None
print(out,round(out.stat().st_size/1e6,2),'MB',len(files),'files')
