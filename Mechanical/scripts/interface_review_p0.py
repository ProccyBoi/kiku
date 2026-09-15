"""Supplementary exact nominal interface checks. Run after housing.py exports."""
import json, pathlib, math
import cadquery as cq
import trimesh
R=pathlib.Path(__file__).resolve().parents[1]
E=R/'exports/P0'
inv=json.loads((E/'part_inventory.json').read_text())
parts={n:cq.Shape.importBrep(str(E/(n+'.brep'))) for n in inv}
def gap(a,b):
    return math.sqrt(sum(max(0,a[i]-b[i+3],b[i]-a[i+3])**2 for i in range(3)))
out={'method':'Exact Boolean at discrete control end positions; conservative AABB separation for antenna and screw broad phase. Nominal geometry only; tolerance and continuous motion not certified.','motion':[],'fastener_to_reference':[]}
for name,travel in [('button_left',1.0),('button_right',1.0),('encoder_knob',.9)]:
    for dz in (0,-travel/2,-travel):
        a=parts[name].translate((0,0,dz))
        v=a.intersect(parts['front_shell']).Volume()
        out['motion'].append({'part':name,'translation_z_mm':dz,'front_shell_overlap_mm3':round(v,6),'status':'CLEAR' if v<.005 else 'COLLISION'})
for n,a in parts.items():
    if not any(k in n for k in ('screw','insert')) or n.startswith('REF_'):continue
    for m,b in parts.items():
        if not m.startswith('REF_') or gap(inv[n]['bounds'],inv[m]['bounds'])>.01:continue
        v=a.intersect(b).Volume()
        out['fastener_to_reference'].append({'fastener':n,'reference':m,'overlap_mm3':round(v,6),'status':'COLLISION' if v>.005 else 'CLEAR'})
ant=[55.34,4.78,2.2,58.74,16.78,2.3]
metal=[n for n in inv if any(k in n for k in ('screw','insert','BATTERY','SPEAKER')) and 'retainer' not in n]
out['antenna']={'trace_bounds_mm':ant,'source':'Antenna trace solids in supplied BM83 STEP','required_mm':15,'conservative_separations_mm':{n:round(gap(ant,inv[n]['bounds']),3) for n in metal}}
bbs=[trimesh.load_mesh(E/(n+'.stl')).bounds for n in ('front_shell','rear_shell','button_left','button_right','encoder_knob')]
mins=[min(b[0][i] for b in bbs) for i in range(3)];maxs=[max(b[1][i] for b in bbs) for i in range(3)]
out['external_envelope_mm']={'method':'Exported STL vertex extrema; mesh tolerance 0.08 mm','min':mins,'max':maxs,'width_height_depth':[round(maxs[i]-mins[i],3) for i in range(3)]}
(R/'review/interface_review_P0.json').write_text(json.dumps(out,indent=2))
print(json.dumps(out,indent=2),flush=True)
