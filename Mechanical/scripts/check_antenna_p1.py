"""Checks specific to the P1 antenna change, including cable vs purchased parts."""
import pathlib,json,math
import cadquery as cq
R=pathlib.Path(__file__).resolve().parents[1];E=R/'exports/P1'
inv=json.loads((E/'part_inventory.json').read_text());geo=json.loads((E/'antenna_geometry.json').read_text())
shapes={n:cq.Shape.importBrep(str(E/(n+'.brep'))) for n in inv}
def bdist(a,b):return math.sqrt(sum(max(0,a[i]-b[i+3],b[i]-a[i+3])**2 for i in range(3)))
def bb(s):
 b=s.BoundingBox();return [b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax]
report={'revision':'P1 antenna integration','geometry':geo,'cable_to_hardware':[],'new_fittings_to_hardware':[],'antenna_to_other_hardware':[],'rf_separation':{},'exceptions':['Cable at its U.FL mating envelope and SMA tail is intentional connection; excluded from cable interference checks.','Reference antenna hinge and fitting shapes are conservative/provisional, not manufacturer CAD.'],'method':'Exact BREP intersections for overlapping bounding boxes; bounding-box separation skips nonadjacent pairs. Conservative trace-to-metal AABB distances. Physical retention strength, fit and RF performance not certified.'}
newrefs=['REF_SMA_female_bulkhead_ENVELOPE','REF_SMA_washer_nut_ENVELOPE','REF_FM_antenna_hinge_UNVERIFIED','REF_FM_antenna_collapsed_BOUND','REF_FM_100mm_RF113_coax','REF_J3_mated_UFL_ENVELOPE']
for n in newrefs:
 a=shapes[n]
 for m,b in shapes.items():
  if m in newrefs or m.startswith(('REF_FM_coax','CHECK_')) or m in ('front_shell','rear_shell'):continue
  if not m.startswith('REF_') and not any(k in m for k in ('screw','insert','guard','carrier','retainer','button','knob','lens')):continue
  if bdist(bb(a),bb(b))>.05:continue
  if n=='REF_J3_mated_UFL_ENVELOPE' and m=='REF_J3':continue
  v=a.intersect(b).Volume()
  key='cable_to_hardware' if n.endswith('RF113_coax') else 'new_fittings_to_hardware'
  report[key].append({'part':n,'against':m,'overlap_mm3':round(v,6),'status':'COLLISION' if v>.005 else 'CLEAR'})
 print('CHECKED',n,flush=True)
ant=[55.34,4.78,2.2,58.74,16.78,2.3]
for n in newrefs:report['rf_separation'][n]=round(bdist(ant,bb(shapes[n])),3)
# Include the full extended position and sampled rotation positions in the RF audit.
for p in E.glob('CHECK_FM_*'):
 if p.suffix!='.brep' or 'cable' in p.stem:continue
 s=cq.Shape.importBrep(str(p));report['rf_separation'][p.stem]=round(bdist(ant,bb(s)),3)
report['rf_separation_requirement_mm']=15
report['rf_min_mm']=min(report['rf_separation'].values())
report['rf_geometric_status']='PASS' if report['rf_min_mm']>=15 else 'REVIEW'
(R/'review/antenna_check_P1.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2),flush=True)
