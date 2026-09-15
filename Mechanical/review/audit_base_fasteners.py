import json,pathlib,math
import cadquery as cq
R=pathlib.Path(__file__).resolve().parents[1]
E=R/'exports/P1'
inv=json.loads((E/'part_inventory.json').read_text())
printed=['front_shell','rear_shell','display_carrier','battery_guard','speaker_retainer','encoder_knob','button_left','button_right','lens']
names=printed+[n for n in inv if any(t in n for t in ('screw','insert')) and not n.startswith('REF_')]
p={n:cq.Shape.importBrep(str(E/(n+'.brep'))) for n in names}
def near(a,b):
 a=inv[a]['bounds'];b=inv[b]['bounds']
 return all(max(a[i],b[i])<=min(a[i+3],b[i+3])+.001 for i in range(3))
out=[]
for n in names[len(printed):]:
 for m in printed:
  if not near(n,m):continue
  s=p[n].intersect(p[m]);v=s.Volume()
  row={'hardware':n,'part':m,'volume':v}
  if v>.001:
   b=s.BoundingBox();row['bounds']=[b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax]
   print(row,flush=True)
  out.append(row)
(R/'review/audit_base_fasteners.json').write_text(json.dumps(out,indent=2))
print('DONE',flush=True)
