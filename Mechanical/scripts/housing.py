"""Editable, parameter driven CadQuery model. Millimetres. See mechanical report.
Run with Mechanical/.venv/Scripts/python.exe Mechanical/scripts/housing.py
P0 contains explicitly provisional purchased-part envelopes; do not treat as released CAD.
"""
import json, pathlib, math
import cadquery as cq
from reference_geometry import ROOT, DATA, pos, box, bounds, load_references
OUT=ROOT/'Mechanical/exports/P0'
OUT.mkdir(exist_ok=True)
P=json.loads((ROOT/'Mechanical/parameters.json').read_text())
FP={f['ref']:f for f in DATA['footprints']}
CONCEPTS={
 '01_Mame':[(6,-7),(36,-7),(59,-4),(66,7),(67,30),(66,54),(65,85),(63,104),(54,111),(26,112),(6,109),(-5,101),(-7,80),(-7,54),(-8,29),(-8,7)],
 '02_Nagare':[(8,-7),(39,-8),(63,-3),(71,15),(73,37),(69,59),(65,84),(63,104),(54,112),(28,114),(6,110),(-4,100),(-6,76),(-7,50),(-6,25),(-3,5)],
 '03_Mini':[(6,-4),(30,-4.2),(56,-4),(64,3),(65,28),(65,55),(65,83),(63,103),(55,109),(30,110),(5,108),(-4,101),(-5,81),(-5,55),(-5,29),(-4,5)]
}
def cyl(x,y,z,r,h):return cq.Solid.makeCylinder(r,h,cq.Vector(x,y,z))
def rr(x,y,z,w,h,t,r):
    return cq.Workplane('XY').box(w,h,t,centered=(True,True,False)).edges('|Z').fillet(r).translate((x,y,z)).val()
def plan(name,z):return cq.Workplane('XY').workplane(offset=z).spline(CONCEPTS[name],periodic=True).close()
def skins(name,envelope=False):
    # Parallel plan offsets with concentric quarter-round lofts avoid fragile
    # edge-fillet operations on a closed periodic B-spline.
    wire=plan(name,0).val()
    edge=wire.Edges()[0]
    stations=[]
    for i in range(96):
        v=edge.positionAt(i/96);t=edge.tangentAt(i/96)
        stations.append((v.x,v.y,-t.y,t.x))
    def profile(inset,z):
        pts=[cq.Vector(x+inset*nx,y+inset*ny,z) for x,y,nx,ny in stations]
        return cq.Wire.assembleEdges([cq.Edge.makeSpline(pts,periodic=True)])
    def bowl(top,radius,sign):
        zc=top-sign*radius;seam=P['seam_z']
        def body(inset,rad,opening):
            rings=[profile(inset,opening)]
            for i in range(7):
                a=math.pi*i/12
                rings.append(profile(inset+rad*(1-math.cos(a)),zc+sign*rad*math.sin(a)))
            return cq.Solid.makeLoft(rings,ruled=True)
        if envelope:return body(0,radius,seam-sign*3.5)
        return body(0,radius,seam).cut(body(P['wall'],radius-P['wall'],seam-sign*.1))
    f=bowl(P['front_z'],3.5,1)
    r=bowl(P['rear_z'],4,-1)
    return f,r
def cosmetic(name):
    f,r=skins(name)
    lens=rr(30.01,82.88,14,54,37.8,1,3.2)
    f=f.cut(rr(30.01,82.88,13.8,54.6,38.4,5,3.5))
    caps=[]
    for ref in ['SW3','SW4']:
        x,y=pos(FP[ref]);c=rr(x,y,15.1,13,9,2.3,3.7);caps.append(c)
        f=f.cut(rr(x,y,12,13.6,9.6,7,4))
    x,y=pos(FP['SW2']);knob=cyl(x,y,18,10.5,9.6)
    f=f.fuse(cyl(x,y,14,13.5,3.6)).cut(cyl(x,y,10,9.1,15))
    return {'front_shell':f,'rear_shell':r,'lens':lens,'button_left':caps[0],'button_right':caps[1],'encoder_knob':knob}
COLORS={'front_shell':(.86,.87,.75),'rear_shell':(.31,.40,.36),'lens':(.08,.14,.15),'button_left':(.91,.51,.23),'button_right':(.85,.85,.70),'encoder_knob':(.25,.35,.29)}
def save_assembly(parts,path):
    a=cq.Assembly(name='Kiku_P0')
    for n,s in parts.items():
        col=COLORS.get(n,(.65,.66,.67))
        if n.startswith('REF_'):col=(.22,.46,.35) if n=='REF_PCB' else (.5,.53,.55)
        a.add(s,name=n,color=cq.Color(*col))
    a.save(str(path))
    return a
def concepts():
    for name in CONCEPTS:
        parts=cosmetic(name);save_assembly(parts,ROOT/f'Mechanical/concepts/{name}.step')
        for n,s in parts.items():cq.exporters.export(s,str(ROOT/f'Mechanical/concepts/{name}_{n}.stl'),tolerance=.12,angularTolerance=.15)
        print('CONCEPT',name,flush=True)
def selected():
    refs,meta=load_references()
    (ROOT/'Mechanical/reference/component_envelopes.json').write_text(json.dumps(meta,indent=2))
    f,r=skins('01_Mame');w=P['wall'];front=P['front_z']
    parts={};checks={}
    # Lens seat: 1.0 mm clear sheet, 0.2 mm recess, 0.2 mm die-cut adhesive.
    lcdx=30.01;lcdy=82.88
    lens=rr(lcdx,lcdy,14,54,37.8,1,3.2)
    f=f.cut(rr(lcdx,lcdy,13.8,54.6,38.4,5,3.5))
    # LCD rotated 180 degrees from landscape drawing so tail meets right-side FPC.
    # Active-area centre is 2.66 mm left of module centre; aperture has .4 mm/edge.
    ax=lcdx-2.66;ay=lcdy
    aperture=rr(ax,ay,8,41.6,31.4,10,.35)
    seat=rr(lcdx,lcdy,12.9,57.4,41.2,.9,3.8).cut(aperture)
    f=f.fuse(seat).cut(aperture)
    refs['LCD_envelope']=box(4.11,64.78,P['lcd_back_z'],51.8,36.2,P['lcd_thickness'])
    # Perimeter carrier rests on backlight frame, with removable support rails.
    carrier=rr(lcdx,lcdy,9.5,53,37.4,.8,1).cut(rr(lcdx,lcdy,9.4,48,33.4,1.2,.5))
    carrier=carrier.cut(box(52,76,9,8,14,3)) # tail exit
    for x in (.7,59.3):
        for ty in ([70] if x<30 else [70,89]):
            carrier=carrier.fuse(box(-.9 if x<30 else 54.4,ty,9.5,6.5,23 if x<30 else 4,.8))
        for y in (72,91):
            carrier=carrier.cut(cyl(x,y,9,1.1,3))
            # Open-to-rear front-shell insert towers outside LCD width.
            f=f.fuse(cyl(x,y,10.3,3.0,3.5)).cut(cyl(x,y,10.2,1.6,4.2))
            parts[f'lcd_screw_{x}_{y}']=cyl(x,y,8.1,1.9,1.4).fuse(cyl(x,y,9.5,1,4))
            parts[f'lcd_insert_{x}_{y}']=cyl(x,y,10.3,1.6,4).cut(cyl(x,y,10.2,.8,4.2))
    parts['display_carrier']=carrier
    # Four PCB mounts use actual H5-H8 centres. Bore opens toward board back.
    for ref in ('H5','H6','H7','H8'):
        x,y=pos(FP[ref]); top=9.2 if y>80 else 13.8
        boss=cyl(x,y,1.6,2.6,top-1.6).cut(cyl(x,y,1.5,1.6,5.0))
        f=f.fuse(boss)
        # Thin bridge to sidewall, above components and below LCD.
        if x<30:bridge=box(-5,y-.7,top-1, x+5,1.4,1)
        else:bridge=box(x,y-.7,top-1,65-x,1.4,1)
        f=f.fuse(bridge)
        parts['PCB_screw_'+ref]=cyl(x,y,-1.4,1.9,1.4).fuse(cyl(x,y,0,1,6))
        parts['PCB_insert_'+ref]=cyl(x,y,1.6,1.6,4).cut(cyl(x,y,1.5,.8,4.2))
    # Four independent rear screws outside board outline; upper/lower left clear RF region.
    for i,(x,y) in enumerate([(-3.5,37),(63,37),(-3.5,80),(63,80)]):
        f=f.fuse(cyl(x,y,-.9,2.6,14.7)).cut(cyl(x,y,-1,1.6,4.2))
        r=r.fuse(cyl(x,y,-16.3,2.6,15.1)).cut(cyl(x,y,-19,1.15,19))
        r=r.cut(cyl(x,y,-18.1,2.1,3.1))
        parts[f'case_screw_{i+1}']=cyl(x,y,-16.5,1.9,1.5).fuse(cyl(x,y,-15,1,18))
        parts[f'case_insert_{i+1}']=cyl(x,y,-.9,1.6,4).cut(cyl(x,y,-1,.8,4.2))
    # Seam alignment tabs; rear pockets accommodate 0.3 mm on either side.
    for x,y in [(-5.6,56),(64,65),(23,111.5),(45,-5.7)]:
        tab=box(x-2,y-.7,-3,4,1.4,3)
        f=f.fuse(tab)
        r=r.cut(box(x-2.3,y-1,-3.4,4.6,2,2.5))
    # Guided floating keycaps: flange cannot pass through aperture; stem floats over switch.
    for ref,label in [('SW3','button_left'),('SW4','button_right')]:
        x,y=pos(FP[ref]);
        cap=rr(x,y,14.15,15,11,.65,3.8).fuse(rr(x,y,14.8,13,9,4,3.4))
        cap=cap.fuse(cyl(x,y,13.8,2,.4))
        guide=rr(x,y,13.4,18.5,14.5,3.7,4.2)
        f=f.fuse(guide).cut(rr(x,y,14.9,13.7,9.7,8,3.75)).cut(rr(x,y,10,15.7,11.7,4.9,4))
        parts[label]=cap
    # SW1/QON remains internal; it is intentionally not exposed as a case control.
    # Encoder reference follows the PEC11L manufacturer dimensions, footprint centre fixed.
    x,y=pos(FP['SW2'])
    refs.pop('SW2',None)
    shaft=cyl(x,y,6.1,3,20).cut(box(x-4,y+1.5,16.1,8,3,11))
    refs['SW2_PEC11L_envelope']=box(x-6.6,y-6,1.6,13.2,12,4.5).fuse(cyl(x,y,6.1,3.5,7)).fuse(shaft)
    island=cyl(x,y,13.4,13.5,4.2).cut(cyl(x,y,10,9.1,12))
    f=f.fuse(island).cut(cyl(x,y,8,9.1,14))
    knob=cq.Workplane('XY').circle(10.5).extrude(10.0).edges('>Z').fillet(1.7).val().translate((x,y,18.8))
    knob=knob.fuse(cyl(x,y,12.8,8.65,6.1))
    dhole=cyl(x,y,12.7,3.12,13.65).cut(box(x-4,y+1.62,12,8,3,16))
    # Full round shaft extends to Z16.1 before the D flat begins. The skirt
    # counterbore clears this round section as well as the threaded bushing.
    knob=knob.cut(dhole).cut(cyl(x,y,12.7,3.7,3.6))
    radial_bore=cq.Solid.makeCylinder(1.6,4.7,cq.Vector(x+5.8,y,23),cq.Vector(1,0,0))
    knob=knob.cut(radial_bore).cut(cq.Solid.makeCylinder(1.05,8,cq.Vector(x+2.8,y,23),cq.Vector(1,0,0)))
    parts['knob_insert']=cq.Solid.makeCylinder(1.6,4,cq.Vector(x+5.8,y,23),cq.Vector(1,0,0)).cut(cq.Solid.makeCylinder(.8,4.2,cq.Vector(x+5.7,y,23),cq.Vector(1,0,0)))
    parts['knob_grub_screw']=cq.Solid.makeCylinder(1,6,cq.Vector(x+3,y,23),cq.Vector(1,0,0))
    parts['encoder_knob']=knob
    # A removable guide ring captures the skirt; tuning shim needed for measured shaft.
    bearing=cyl(x,y,10.8,11.8,2.7).cut(cyl(x,y,10.7,8.95,3))
    f=f.fuse(bearing)
    # Model-based openings and complete insertion corridors, measured in connector axes.
    jb=bounds(refs['J1']);usb_x=(jb[0]+jb[3])/2
    mouth=min([face for face in refs['J1'].Faces() if face.geomType()=='PLANE' and abs(face.normalAt().y)>.99 and face.Area()>1],key=lambda face:face.Center().y)
    usb_z=mouth.Center().z
    usb=box(usb_x-6.5,-18,usb_z-3.5,13,18+max(0,jb[1])+.6,7)
    # Broad overmould relief is deliberately deep enough to reach the metal mouth.
    f=f.cut(usb);r=r.cut(usb);checks['USB_C_13x7_plug_corridor']=usb
    jack=bounds(refs['J6']);jx=pos(FP['J6'])[0];jz=4.1 # STEP cylindrical bore axis, radius 1.75
    hp=cq.Solid.makeCylinder(5.5,24,cq.Vector(jx,-20,jz),cq.Vector(0,1,0))
    f=f.cut(hp);r=r.cut(hp);checks['headphone_11mm_overmould_corridor']=hp
    sd=bounds(refs['J2']);sx=(sd[0]+sd[3])/2
    sdcut=box(sx-8.7,102,1.25,17.4,25,3.4)
    f=f.cut(sdcut);r=r.cut(sdcut);checks['microSD_insertion_corridor']=sdcut
    # Rear finger relief along the SD corridor.
    relief=box(sx-9,101,-4.5,18,26,12.5);f=f.cut(relief);r=r.cut(relief)
    checks['microSD_finger_access']=relief
    # User-confirmed 34x50x10 mm cell; supplier tolerance/lead exit still unconfirmed.
    bx,by,bz=P['battery_origin'];bw,bh,bt=P['battery_envelope']
    refs['BATTERY_34x50x10']=box(bx,by,bz,bw,bh,bt)
    for rx,ry,dx,dy in [(bx-1.7,by-1.7,bw+3.4,1.2),(bx-1.7,by+bh+.5,bw+3.4,1.2),(bx-1.7,by,1.2,bh),(bx+bw+.5,by,1.2,bh)]:
        r=r.fuse(box(rx,ry,-16.3,dx,dy,9.8))
    # Removable insulating tray above cell; 1.4 mm reserved headspace, no preload on pouch.
    tray=rr(bx+bw/2,by+bh/2,-3.8,bw+3.4,bh+3.4,.8,1.2)
    for tx in (bx-3.6,bx+bw+3.6):
        for ty in (by+6,by+bh-6):
            r=r.fuse(cyl(tx,ty,-16.3,2.7,12.5)).cut(cyl(tx,ty,-7.9,1.6,4.2))
            tray=tray.fuse(cyl(tx,ty,-3.8,2.5,.8)).cut(cyl(tx,ty,-4,1.1,1.2))
            parts[f'guard_screw_{tx}_{ty}']=cyl(tx,ty,-3,1.9,1.4).fuse(cyl(tx,ty,-7,1,4))
            parts[f'guard_insert_{tx}_{ty}']=cyl(tx,ty,-7.8,1.6,4).cut(cyl(tx,ty,-7.9,.8,4.2))
    parts['battery_guard']=tray
    # Rear-firing speaker cup: retained at rim by a removable plate, cone faces grille.
    spx,spy=P['speaker_center'];sr=P['speaker_diameter']/2
    refs['SPEAKER_C50387209']=cyl(spx,spy,-14.8,sr,P['speaker_height'])
    cup=cyl(spx,spy,-16.3,sr+2,8.5).cut(cyl(spx,spy,-15.3,sr+.5,7.6))
    cup=cup.cut(box(spx-2,spy+9.4,-15.4,4,4,7.8)) # undimensioned pull-tab relief, fit check
    r=r.fuse(cup)
    for i in range(-3,4):
        length=2*math.sqrt(max(0,8.5**2-(i*2.35)**2))
        slot=rr(spx,spy+i*2.35,-19,length,1.1,4,.5);r=r.cut(slot)
    cover=cyl(spx,spy,-7.8,sr+2,.8)
    for tx in (spx-15,spx+15):
        cover=cover.fuse(box(min(tx,spx),spy-2,-7.8,abs(tx-spx),4,.8)).cut(cyl(tx,spy,-8.2,1.1,2))
        r=r.fuse(cyl(tx,spy,-16.3,2.7,8.5)).cut(cyl(tx,spy,-11.9,1.6,4.2))
        parts[f'speaker_screw_{tx}']=cyl(tx,spy,-7,1.9,1.4).fuse(cyl(tx,spy,-11,1,4))
        parts[f'speaker_insert_{tx}']=cyl(tx,spy,-11.8,1.6,4).cut(cyl(tx,spy,-11.9,.8,4.2))
    # Small wire notch uses compliant seal on assembly.
    cover=cover.cut(box(spx-1,spy-sr-3,-8.2,2,4,2))
    parts['speaker_retainer']=cover
    # Measured FM connector XY, with a reserved coax route up right side, no sharp corners.
    checks['FPC_route_UNVERIFIED']=box(54,79.1,3.6,6.0,7.5,6.85)
    # Coax runs beneath the PCB on the right, away from the lower Bluetooth antenna.
    for cy in (74,94):
        guide=box(57,cy-2,-16.3,3,4,13.6)
        groove=cq.Solid.makeCylinder(1,4.2,cq.Vector(58.5,cy-2.1,-3.7),cq.Vector(0,1,0))
        guide=guide.cut(groove).cut(box(57.8,cy-2.1,-3.7,1.4,4.2,1.2))
        r=r.fuse(guide)
    refs['FM_coax_route_PROVISIONAL']=cq.Solid.makeCylinder(.7,34,cq.Vector(58.5,68,-3.7),cq.Vector(0,1,0))
    # Simple separate compliant interfaces; not rigid prints.
    parts['speaker_rim_gasket']=cyl(spx,spy,-15.3,sr,.5).cut(cyl(spx,spy,-15.4,sr-1.2,.7))
    parts['speaker_rear_foam']=cyl(spx,spy,-9.7,sr,1.9).cut(cyl(spx,spy,-9.8,sr-1.2,2.1))
    parts['battery_foam']=box(bx+2,by+2,-16.2,bw-4,bh-4,1)
    # Reapply guides after the encoder island joins the control region, preserving
    # both keycap swept spaces where the sculpted control regions overlap.
    for ref in ('SW3','SW4'):
        x,y=pos(FP[ref])
        f=f.cut(rr(x,y,15.1,13.7,9.7,8,3.75)).cut(rr(x,y,10,15.7,11.7,5.1,4))
    # Clip mounting bridges and seam tabs to the actual outer lofts. Preserve
    # deliberately raised front controls by adding their local envelope.
    fouter,router=skins('01_Mame',envelope=True)
    for ref in ('SW3','SW4'):
        x,y=pos(FP[ref]);fouter=fouter.fuse(rr(x,y,13.4,18.5,14.5,3.7,4.2))
    x,y=pos(FP['SW2']);fouter=fouter.fuse(cyl(x,y,13.4,13.5,4.2))
    parts['front_shell']=f.intersect(fouter).clean();parts['rear_shell']=r.intersect(router).clean();parts['lens']=lens
    return parts,refs,checks,meta
if __name__=='__main__':
    import sys
    if '--concepts' in sys.argv:concepts()
    else:
        parts,refs,checks,meta=selected()
        for name,shape in parts.items():
            assert shape.isValid() and len(shape.Solids())==1 and shape.Volume()>.01, f'Unprintable {name}: {len(shape.Solids())} solids'
        for n,s in parts.items():
            print(n,'valid',s.isValid(),'solids',len(s.Solids()),'volume',round(s.Volume(),2),flush=True)
            cq.exporters.export(s,str(OUT/f'{n}.step'))
            if 'screw' not in n and 'insert' not in n and 'gasket' not in n and 'foam' not in n and n!='lens':
                cq.exporters.export(s,str(OUT/f'{n}.stl'),tolerance=.08,angularTolerance=.1)
        allparts={**parts,**{'REF_'+n:s for n,s in refs.items()}}
        save_assembly(allparts,OUT/'Kiku_P0_assembly.step')
        save_assembly(parts,OUT/'Kiku_P0_housing.step')
        # Intermediate BREP supports deterministic clearance/visual QA without rebuilding.
        for n,s in allparts.items():s.exportBrep(str(OUT/f'{n}.brep'))
        for n,s in checks.items():s.exportBrep(str(OUT/f'CHECK_{n}.brep'))
        (OUT/'part_inventory.json').write_text(json.dumps({n:{'bounds':bounds(s),'volume_mm3':s.Volume(),'valid':s.isValid(),'solids':len(s.Solids())} for n,s in allparts.items()},indent=2))
