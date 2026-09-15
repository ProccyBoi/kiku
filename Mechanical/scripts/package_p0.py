"""Package only the current P0 inventory; never include stale alternate exports."""
import pathlib,json,zipfile,hashlib,html
R=pathlib.Path(__file__).resolve().parents[1]
E=R/'exports/P0'
inv=json.loads((E/'part_inventory.json').read_text())
files=[]
for p in E.iterdir():
    if (p.stem in inv and not p.stem.startswith('REF_')) or p.name in ('Kiku_P0_housing.step','part_inventory.json') or p.stem.startswith('CHECK_'):
        if p.suffix in ('.stl','.step','.brep','.json'):files.append((p,'Mechanical/exports/P0/'+p.name))
for name in ('housing.py','reference_geometry.py','extract_board.py','verify.py','interface_review_p0.py','render_p0.py','package_p0.py'):
    files.append((R/'scripts'/name,'Mechanical/scripts/'+name))
for name in ('parameters.json','requirements-P0.txt','P0_DESIGN_REPORT.md','P0_REVIEW.html'):
    files.append((R/name,'Mechanical/'+name))
for p in (R/'concepts').iterdir():
    if p.suffix in ('.png','.step','.stl'):files.append((p,'Mechanical/concepts/'+p.name))
for p in (R/'review').iterdir():
    if p.name.startswith('P0_') or p.name in ('clearance_review.json','interface_review_P0.json'):
        files.append((p,'Mechanical/review/'+p.name))
for name in ('board_datums.json','component_envelopes.json','pcb_bare.step'):
    files.append((R/'reference'/name,'Mechanical/reference/'+name))
files.append((R/'REFERENCE_GEOMETRY.md','Mechanical/REFERENCE_GEOMETRY.md'))
for src,arc in ((R.parent/'LICENSE','LICENSE'),(R.parent/'LICENSES/CERN-OHL-P-2.0.txt','LICENSES/CERN-OHL-P-2.0.txt'),(R.parent/'LICENSES/MIT.txt','LICENSES/MIT.txt')):
    files.append((src,arc))
qa=json.loads((R/'review/clearance_review.json').read_text())
extra=json.loads((R/'review/interface_review_P0.json').read_text())
assert not [s for s in qa['solids'].values() if not s['valid']]
assert not [s for s in qa['stl'].values() if not s['watertight'] or not s['winding_consistent']]
assert not [s for s in qa['shell_to_reference'] if s['status']=='COLLISION']
assert not qa['printed_part_pairs']
assert not [s for s in qa['corridors'] if s['status']=='COLLISION']
assert not [s for s in extra['motion']+extra['fastener_to_reference'] if s['status']=='COLLISION']
images=[('Assembled perspective','P0_assembled_iso.png'),('Front','P0_assembled_front.png'),('Rear / speaker grille','P0_assembled_rear.png'),('Profile','P0_assembled_side.png'),('Exploded assembly','P0_exploded.png'),('Longitudinal section at X = 30 mm','P0_section_longitudinal.png'),('Control section at Y = 55 mm','P0_section_controls.png'),('Speaker section at X = 18 mm','P0_section_speaker.png'),('Front interior','P0_front_inside.png'),('Rear interior','P0_rear_inside.png')]
cards=''.join(f'<figure><a href="review/{p}"><img loading="lazy" src="review/{p}" alt="{t}"></a><figcaption>{t}</figcaption></figure>' for t,p in images)
dims=' × '.join(f'{v:.1f}' for v in extra['external_envelope_mm']['width_height_depth'])
page='''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Kiku / Mame P0</title><style>
*{box-sizing:border-box}body{margin:0;background:#f1f0e9;color:#26392f;font:17px/1.6 system-ui,sans-serif}main{max-width:1160px;margin:auto;padding:48px 24px}h1{font-size:64px;letter-spacing:-3px;line-height:1.05;margin:20px 0}h2{font-size:28px;margin:56px 0 16px}p{max-width:850px}.eyebrow{letter-spacing:2px;font-size:12px;text-transform:uppercase}.tag{background:#dfceac;padding:8px 14px;border-radius:30px;font-size:13px;display:inline-block}a{color:#285344;text-underline-offset:4px}.links{display:flex;gap:12px;flex-wrap:wrap;margin:24px 0}.links a{padding:10px 18px;border:1px solid #7b8a78;border-radius:24px;text-decoration:none}.grid{display:grid;grid-template-columns:1fr 1fr;gap:20px}figure{margin:0;overflow:hidden;border-radius:14px;background:#e5e5d9}img{width:100%;display:block}figcaption{padding:14px 20px;font-size:14px}.facts{display:flex;gap:40px;flex-wrap:wrap;border-top:1px solid #bbc3b5;border-bottom:1px solid #bbc3b5;padding:24px 0}.facts strong{font-size:24px;display:block}.facts span{font-size:13px}footer{margin-top:48px;font-size:13px}@media(max-width:650px){h1{font-size:46px}.grid{grid-template-columns:1fr}}
</style><main><div class="eyebrow">Blobject / Mechanical design / P0</div><h1>Kiku. A softer kind<br>of music player.</h1><span class="tag">Engineering development · physical fit validation pending</span><p>Mame pairs a softly asymmetric ivory front with a muted green rear, a recessed smoked lens and distinct tactile controls. The existing PCB positions are retained. The selected speaker is LCSC C50387209; the battery envelope is 34 × 50 × 10 mm.</p><div class="links"><a href="exports/P0/Kiku_P0_housing.step">Housing STEP</a><a href="P0_DESIGN_REPORT.md">Mechanical report</a><a href="REFERENCE_GEOMETRY.md">Reference setup</a><a href="scripts/housing.py">Editable CAD source</a><a href="review/clearance_review.json">Geometry checks</a><a href="review/interface_review_P0.json">Interface checks</a></div>'''
page+=f'<div class="facts"><div><strong>{dims} mm</strong><span>Approx. width × height × depth including knob</span></div><div><strong>1.8 mm</strong><span>Nominal shell wall</span></div><div><strong>8 parts</strong><span>Individual prototype STL exports</span></div></div>'
page+='<h2>Three interpretations</h2><p>Front, rear, profile and perspective studies. Mame was selected for its balance of character, grip and assembly space; the alternatives and scoring are documented in the report.</p><a href="concepts/three_concepts.png"><img src="concepts/three_concepts.png" alt="Mame, Nagare and Mini concepts"></a><h2>The developed assembly</h2><div class="grid">'+cards+'</div>'
page+='<h2>Verification and next physical checks</h2><p>The current geometric review found no overlaps in its screened rigid-part pairs or the explicit connector/FPC corridors. All eight STL meshes are watertight with consistent winding. Discrete control positions and added fasteners are checked separately. Surface screening does not certify minimum clearance, full containment or tolerance stacks.</p><p>Confirm the real display tail fold, switch actuation and return, purchased inserts, battery lead exit, speaker tab and sealing, FM antenna installation and RF performance before committing to use or tooling. SW1/QON is internal in this version. The report identifies the remaining moulding changes.</p><footer>Dimensions are millimetres. The recorded QA used locally obtained hardware models and explicitly identified bounding envelopes; third-party model solids are not redistributed in this package. P0 is a prototype engineering package, not a production release.</footer></main></html>'
(R/'P0_REVIEW.html').write_text(page,encoding='utf-8')
readme='''KIKU / MAME P0 — ENGINEERING DEVELOPMENT PACKAGE

Open Mechanical/P0_REVIEW.html for the visual index and Mechanical/P0_DESIGN_REPORT.md
for dimensions, hardware basis, assembly, verification and remaining validation.

Housing assembly: Mechanical/exports/P0/Kiku_P0_housing.step.
Print the 8 current STL files; the lens, foam, gaskets and fasteners are secondary parts.
STLs are in millimetres and assembly coordinates; orient them in your slicer.
Native editable model: Mechanical/scripts/housing.py and Mechanical/parameters.json.

This public package intentionally excludes populated/frozen component geometry.
Mechanical/REFERENCE_GEOMETRY.md explains how to obtain those local third-party
inputs under their original terms before rerunning the full strict fit review.
The checked-in QA JSON records the reviewed result without redistributing the
source model solids. The PCB datum extractor requires the KiCad project and
KiCad Python.
See LICENSE for the mixed-licence scope and LICENSES/ for the full licence texts.
Do not combine this P0 package with other housing revisions.

This is not production-released or tolerance-certified. Physical FPC routing,
switch travel, battery leads, inserts, acoustics and RF checks remain necessary.
The report identifies tooling changes still required.
'''
manifest={arc:hashlib.sha256(p.read_bytes()).hexdigest() for p,arc in files}
out=R/'Kiku_Mame_P0_design_package.zip'
with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
    z.writestr('START_HERE.txt',readme)
    z.writestr('SHA256_manifest.json',json.dumps(manifest,indent=2))
    for p,arc in files:z.write(p,arc)
with zipfile.ZipFile(out) as z:
    assert z.testzip() is None
print(out,round(out.stat().st_size/1e6,2),'MB',len(files),'files')
