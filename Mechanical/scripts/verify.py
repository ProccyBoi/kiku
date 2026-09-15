"""Geometric QA: solid validity, connected shells, STL manifoldness, interface collisions.
Missing or envelope-only purchased-part geometry remains explicitly unverified.
"""
import pathlib,json,itertools,math
import cadquery as cq
import trimesh
import vtk
ROOT=pathlib.Path(__file__).resolve().parents[2]/'Mechanical'
def bounds(s):
    b=s.BoundingBox();return (b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax)
def bb_dist(a,b):
    a,b=bounds(a),bounds(b)
    return math.sqrt(sum(max(0,a[i]-b[i+3],b[i]-a[i+3])**2 for i in range(3)))
inventory=json.loads((ROOT/'exports/P0/part_inventory.json').read_text())
parts={p.stem:cq.Shape.importBrep(str(p)) for p in (ROOT/'exports/P0').glob('*.brep') if p.stem in inventory or p.stem.startswith('CHECK_')}
meshes={}
def mesh(n):
    if n not in meshes:
        vs,fs=parts[n].tessellate(.06,.10);pts=vtk.vtkPoints();cells=vtk.vtkCellArray()
        for v in vs:pts.InsertNextPoint(*v.toTuple())
        for tri in fs:
            cells.InsertNextCell(3)
            for i in tri:cells.InsertCellPoint(i)
        poly=vtk.vtkPolyData();poly.SetPoints(pts);poly.SetPolys(cells);meshes[n]=poly
    return meshes[n]
def candidate(a,b):
    filt=vtk.vtkCollisionDetectionFilter();filt.SetInputData(0,mesh(a));filt.SetInputData(1,mesh(b))
    for i in (0,1):
        t=vtk.vtkTransform();t.Identity();filt.SetTransform(i,t)
    filt.SetCollisionModeToFirstContact();filt.SetBoxTolerance(.01);filt.SetCellTolerance(.0001);filt.Update()
    return filt.GetNumberOfContacts()>0
report={'release_status':'HOLD - prototype engineering review','method':'0.06 mm tessellation broad-phase surface collision screening; exact BREP volume on candidates. Complete containment and tolerance stack require separate review. This is not a certified interference-free assembly.','solids':{},'stl':{},'shell_to_reference':[],'printed_part_pairs':[],'corridors':[]}
for n,s in parts.items():report['solids'][n]={'valid':s.isValid(),'solids':len(s.Solids()),'volume':round(s.Volume(),4)}
for p in (ROOT/'exports/P0').glob('*.stl'):
    if p.stem not in inventory:continue
    m=trimesh.load_mesh(p)
    report['stl'][p.name]={'watertight':bool(m.is_watertight),'winding_consistent':bool(m.is_winding_consistent),'bodies':int(m.body_count),'volume':float(m.volume)}
for shell in ['front_shell','rear_shell','display_carrier','battery_guard','speaker_retainer','button_left','button_right','encoder_knob']:
    for ref,s in parts.items():
        if not ref.startswith('REF_'):continue
        a=parts[shell];d=bb_dist(a,s)
        if d>.6:continue
        vol=a.intersect(s).Volume() if candidate(shell,ref) else 0
        report['shell_to_reference'].append({'part':shell,'ref':ref,'intersection_mm3':round(vol,5),'distance_mm':None,'status':'COLLISION' if vol>.005 else 'NO_OVERLAP - minimum clearance not measured'})
        if vol>.005:print('COLLISION',shell,ref,vol,flush=True)
    print('CHECKED',shell,flush=True)
printed=[n for n in parts if not n.startswith(('REF_','CHECK_')) and not any(k in n for k in ('screw','insert','gasket','foam'))]
for n,m in itertools.combinations(printed,2):
    a,b=parts[n],parts[m]
    if bb_dist(a,b)>.01:continue
    vol=a.intersect(b).Volume() if candidate(n,m) else 0
    if vol>.005:report['printed_part_pairs'].append({'a':n,'b':m,'intersection_mm3':round(vol,5)})
for n,s in parts.items():
    if not n.startswith('CHECK_'):continue
    for shell in ('front_shell','rear_shell'):
        v=parts[shell].intersect(s).Volume()
        report['corridors'].append({'corridor':n,'shell':shell,'intersection_mm3':round(v,5),'status':'CLEAR' if v<.005 else 'COLLISION'})
(ROOT/'review/clearance_review.json').write_text(json.dumps(report,indent=2))
print('COLLISIONS',json.dumps([x for x in report['shell_to_reference'] if x['status']=='COLLISION']+report['printed_part_pairs'],indent=2))
print('INVALID',[n for n,s in report['solids'].items() if not s['valid']]);print('CORRIDORS',report['corridors'])
