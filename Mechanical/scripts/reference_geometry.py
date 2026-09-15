import pathlib,json,re,math,os,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
DATA=json.loads((ROOT/'Mechanical/reference/board_datums.json').read_text())
KICAD=pathlib.Path(os.environ.get('KICAD9_3DMODEL_DIR','C:/Program Files/KiCad/9.0/share/kicad/3dmodels'))
def source_path(m):return pathlib.Path(m['path'].replace('${KIPRJMOD}',str(ROOT)).replace('${KICAD9_3DMODEL_DIR}',str(KICAD)))
def source_inventory():
    rows=[]
    for f in DATA['footprints']:
        for m in f['models']:
            p=source_path(m)
            rows.append({'ref':f['ref'],'value':f['value'],'source':m['path'],'resolved':str(p),'exists':p.is_file(),'allow_missing':f['ref']=='D3'})
    return rows
def check_sources():
    rows=source_inventory();missing=[r for r in rows if not r['exists'] and not r['allow_missing']]
    for r in missing:print('MISSING',r['ref'],r['source'])
    if missing:
        print(f'Reference source check: FAIL ({len(missing)} missing; D3 synthetic allowance excluded)')
        return False
    print(f'Reference source check: PASS ({sum(r["exists"] for r in rows)} source files; D3 may use documented synthetic allowance)')
    return True
if __name__=='__main__' and '--check-sources' in sys.argv:
    raise SystemExit(0 if check_sources() else 1)

# CadQuery/OCC is intentionally imported only for geometry operations. The
# source-audit path above therefore works on machines that have not installed
# the mechanical CAD environment yet.
import cadquery as cq
import numpy as np
def pos(f):return (f['xy'][0]-72.11,157.90-f['xy'][1])
def box(x,y,z,dx,dy,dz):return cq.Workplane('XY').box(dx,dy,dz,centered=False).translate((x,y,z)).val()
def bounds(s):
    b=s.BoundingBox();return [round(v,5) for v in (b.xmin,b.ymin,b.zmin,b.xmax,b.ymax,b.zmax)]
def transform(s,f,m):
    # KiCad model rotations are clockwise; footprint orientation is CCW in mechanical XY.
    for axis,angle in zip(((1,0,0),(0,1,0),(0,0,1)),m['rotation']):
        if angle:s=s.rotate((0,0,0),axis,-angle)
    s=s.translate(tuple(m['offset'])).rotate((0,0,0),(0,0,1),f['angle'])
    return s.translate((*pos(f),1.6))
def load_references(allow_frozen=True):
    refs={'PCB':cq.importers.importStep(str(ROOT/'Mechanical/reference/pcb_bare.step')).val()};meta=[]
    for f in DATA['footprints']:
        for m in f['models']:
            p=source_path(m);kind='STEP solid'
            if not p.exists():
                frozen=ROOT/'Mechanical/reference/frozen'/f"{f['ref']}.brep"
                if allow_frozen and frozen.exists():
                    s=cq.Shape.importBrep(str(frozen));refs[f['ref']]=s
                    meta.append({'ref':f['ref'],'value':f['value'],'source':m['path'],'kind':'Frozen transformed reference; see component_envelopes.json for original fidelity','bounds':bounds(s)})
                    continue
                if f['ref']=='D3':s=box(-.45,-.25,0,.9,.5,.5);kind='UNVERIFIED missing-model allowance'
                else:continue
            elif p.suffix.lower() in ('.step','.stp'):
                s=cq.importers.importStep(str(p)).val()
            else:
                pts=[]
                for block in re.findall(r'point\s*\[([^]]+)\]',p.read_text()):
                    nums=[float(n) for n in re.findall(r'[-+]?\d*\.?\d+(?:[eE][-+]?\d+)?',block)]
                    pts.extend(np.array(nums).reshape(-1,3)*2.54)
                if not pts:continue
                pts=np.array(pts);lo=pts.min(axis=0);hi=pts.max(axis=0)
                s=box(*lo,*(hi-lo));kind='VRML-derived bounding envelope (not exact solid)'
            s=transform(s,f,m)
            refs[f['ref']]=s
            # Preserve the KiCad model expression rather than the resolved host path so
            # generated metadata stays portable and does not publish workstation paths.
            meta.append({'ref':f['ref'],'value':f['value'],'source':m['path'],'kind':kind,'bounds':bounds(s)})
    return refs,meta
if __name__=='__main__':
    if '--freeze-local' in sys.argv:
        if not check_sources():raise SystemExit(1)
        refs,meta=load_references(allow_frozen=False)
        out=ROOT/'Mechanical/reference/frozen';out.mkdir(parents=True,exist_ok=True)
        for ref,shape in refs.items():
            if ref!='PCB':shape.exportBrep(str(out/f'{ref}.brep'))
        (ROOT/'Mechanical/reference/component_envelopes.json').write_text(json.dumps(meta,indent=2))
        print(f'Wrote {len(refs)-1} local transformed reference BREPs to {out}')
        raise SystemExit(0)
    refs,meta=load_references()
    (ROOT/'Mechanical/reference/component_envelopes.json').write_text(json.dumps(meta,indent=2))
    for m in meta:
        if m['ref'].startswith(('J','SW','U1','BT','LS','FPC')):print(m['ref'],m['bounds'],m['kind'])
    print('PCB',bounds(refs['PCB']))
