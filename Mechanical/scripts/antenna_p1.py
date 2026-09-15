"""Mame P1: actual FM antenna accommodation added to the P0 native model.
Run from package root with the CadQuery environment. Millimetres.
Measured listing dimensions and provisional fitting dimensions are kept distinct.
"""
import pathlib,json,math
import cadquery as cq
import housing as base
from reference_geometry import ROOT,box,bounds
R=ROOT/'Mechanical';OUT=R/'exports/P1';OUT.mkdir(parents=True,exist_ok=True)
P=json.loads((R/'antenna-P1.json').read_text())
def cx(x,y,z,r,h):return cq.Solid.makeCylinder(r,h,cq.Vector(x,y,z),cq.Vector(1,0,0))
def rounded_path(points,radius):
    pts=[cq.Vector(*p) for p in points];edges=[];last=pts[0]
    trims=[]
    for i in range(1,len(pts)-1):
        u=(pts[i]-pts[i-1]).normalized();v=(pts[i+1]-pts[i]).normalized()
        theta=math.acos(max(-1,min(1,u.dot(v))));t=radius*math.tan(theta/2)
        a=pts[i]-u*t;b=pts[i]+v*t
        normal=u.cross(v).normalized();c=a+normal.cross(u)*radius
        mid=c+((a-c)+(b-c)).normalized()*radius
        trims.append(t)
        if (a-last).Length>1e-6:edges.append(cq.Edge.makeLine(last,a))
        edges.append(cq.Edge.makeThreePointArc(a,mid,b));last=b
    for i in range(1,len(trims)):
        assert trims[i-1]+trims[i] < (pts[i+1]-pts[i]).Length,'Bends overlap'
    assert trims[0]<(pts[1]-pts[0]).Length and trims[-1]<(pts[-1]-pts[-2]).Length
    edges.append(cq.Edge.makeLine(last,pts[-1]));return cq.Wire.assembleEdges(edges)
def tube(path,r):
    e=path.Edges()[0];plane=cq.Plane(origin=e.startPoint(),normal=e.tangentAt(0))
    return cq.Workplane(plane).circle(r).sweep(cq.Workplane(obj=path),isFrenet=True).val()
def add_antenna(parts,refs,checks):
    r=parts['rear_shell'];f=parts['front_shell']
    # Delete the unused P0 coax guides above the rear floor and its reference.
    for y in (74,94):r=r.cut(box(56.9,y-2.1,-16.2,3.2,4.2,16))
    refs.pop('FM_coax_route_PROVISIONAL',None)
    x=P['bulkhead_face_x_mm'];y=P['bulkhead_y_mm'];z=P['bulkhead_z_mm'];h=P['bulkhead_hole_diameter_mm']/2
    # Flat outer land, internal socket recess and a small entrance lead-in.
    pod=cx(x,y,z,6.4,6.0)
    r=r.fuse(pod).cut(cx(x-.2,y,z,h,10))
    r=r.cut(cx(x+P['panel_land_thickness_mm'],y,z,5.0,8))
    lead=cq.Solid.makeCone(h+.35,h,.35,cq.Vector(x,y,z),cq.Vector(1,0,0));r=r.cut(lead)
    refs['SMA_female_bulkhead_ENVELOPE']=cx(x-4.0,y,z,3.175,7).fuse(cx(x+3,y,z,4.5,1)).fuse(cx(x+4,y,z,1.8,1))
    # Washer/nut are conservative circular envelopes, not verified hex profiles.
    refs['SMA_washer_nut_ENVELOPE']=cx(x-1.8,y,z,4.5,1.8).cut(cx(x-1.9,y,z,3.25,2))
    pivot=(x-P['hinge_offset_from_panel_mm'],y,z)
    refs['FM_antenna_hinge_UNVERIFIED']=cx(pivot[0],y,z,4.5,P['hinge_offset_from_panel_mm']-4.0)
    arm=cq.Solid.makeCylinder(4.5,P['antenna_collapsed_overall_mm'],cq.Vector(*pivot),cq.Vector(0,-1,0))
    refs['FM_antenna_collapsed_BOUND']=arm
    # Open outward saddles allow the whip to swing away from the left side.
    for cy in P['cradle_y_mm']:
        cup=cq.Solid.makeCylinder(6.65,6,cq.Vector(pivot[0],cy-3,z),cq.Vector(0,1,0))
        bridge=box(pivot[0],cy-3,z-2,-4.5-pivot[0],6,4)
        cup=cup.fuse(bridge).cut(cq.Solid.makeCylinder(4.85,6.4,cq.Vector(pivot[0],cy-3.2,z),cq.Vector(0,1,0)))
        cup=cup.cut(box(pivot[0]-10,cy-4,z-8,10,8,16))
        r=r.fuse(cup)
    route=rounded_path(P['route_points_mm'],P['routing_bend_radius_mm']);wire=tube(route,P['coax_diameter_mm']/2)
    clearance=tube(route,P['coax_diameter_mm']/2+.4)
    refs['FM_100mm_RF113_coax']=wire
    # Mating axis from the supplied J3 STEP cylindrical surfaces, not the
    # footprint origin (which is offset 0.76 mm from the connector centre).
    refs['J3_mated_UFL_ENVELOPE']=base.cyl(58.155,66.55,2.85,1.6,1.65)
    # Two open cable saddles supported from the rear's left/right perimeter.
    # Their channels are cut by the same smooth cable clearance sweep.
    guide_locations=[]
    for station,wallx in [(.18,64.5),(.80,-4.5)]:
        p=route.positionAt(station);bx,by,bz=p.toTuple();guide_locations.append(p.toTuple())
        lo=min(bx-1.9,wallx);hi=max(bx+1.9,wallx)
        support=box(lo,by-1.4,-16.2,hi-lo,2.8,bz-1.1+16.2)
        guide_top=min(bz+.6,-1.2) # remain below the shell seam and PCB back
        guide=box(bx-1.9,by-1.4,bz-1.5,3.8,2.8,guide_top-bz+1.5)
        # Lay-in slot: fittings never have to be threaded through a closed hole.
        guide=guide.cut(box(bx-.85,by-1.5,bz,1.7,3.0,2.0))
        r=r.fuse(support).fuse(guide)
    r=r.cut(clearance)
    # The diagonal sweep can leave microscopic detached roof corners at the
    # right guide. Remove only that diagnosed debris, never structural solids.
    solids=sorted(r.Solids(),key=lambda s:s.Volume(),reverse=True);removed=[]
    if len(solids)>1:
        for s in solids[1:]:
            b=bounds(s)
            assert s.Volume()<.01 and 56<b[0]<61 and 73<b[1]<78 and -1.6<b[2]<-1.1,('Unexpected disconnected feature',s.Volume(),b)
            removed.append({'volume_mm3':s.Volume(),'bounds_mm':b})
        r=solids[0]
    # Both shells clear the bend around the PCB edge; board remains fixed.
    f=f.cut(clearance)
    parts['front_shell']=f.clean();parts['rear_shell']=r.clean()
    checks['FM_cable_clearance_R4']=clearance
    for angle in (0,15,30,45,60,75,90):
        checks[f'FM_fold_pose_{angle:02d}']=arm.rotate(pivot,(pivot[0],pivot[1],pivot[2]+1),-angle)
    checks['FM_extended_outward']=cq.Solid.makeCylinder(4.5,P['antenna_extended_overall_mm'],cq.Vector(*pivot),cq.Vector(-1,0,0))
    info={'route_length_mm':route.Length(),'nominal_cable_length_mm':P['pigtail_length_mm'],'length_remainder_mm':P['pigtail_length_mm']-route.Length(),'bend_radius_mm':P['routing_bend_radius_mm'],'guide_locations_mm':guide_locations,'removed_guide_slivers':removed,'pivot_mm':pivot,'qualifier':'105/310 mm overall lengths conservatively used as complete rod envelopes; hinge and fittings not fully dimensioned by listing. 100 mm usable cable measurement and minimum bend radius require hardware confirmation.'}
    (OUT/'antenna_geometry.json').write_text(json.dumps(info,indent=2));print('ANTENNA',info,flush=True)
    return parts,refs,checks
if __name__=='__main__':
    import sys
    if '--cached-base' in sys.argv:
        src=R/'exports/P0';inv=json.loads((src/'part_inventory.json').read_text())
        shapes={n:cq.Shape.importBrep(str(src/(n+'.brep'))) for n in inv}
        parts={n:s for n,s in shapes.items() if not n.startswith('REF_')}
        refs={n[4:]:s for n,s in shapes.items() if n.startswith('REF_')}
        checks={p.stem[6:]:cq.Shape.importBrep(str(p)) for p in src.glob('CHECK_*.brep')}
    else:parts,refs,checks,_=base.selected()
    parts,refs,checks=add_antenna(parts,refs,checks)
    if '--diagnose' in sys.argv:
        for n in ('front_shell','rear_shell'):
            print(n,[(s.Volume(),bounds(s)) for s in parts[n].Solids()],flush=True)
            parts[n].exportBrep(str(OUT/f'_debug_{n}.brep'))
        sys.exit(0)
    for n,s in parts.items():
        assert s.isValid() and len(s.Solids())==1 and s.Volume()>.01,(n,s.isValid(),len(s.Solids()))
    for n,s in parts.items():
        assert s.isValid() and len(s.Solids())==1 and s.Volume()>.01,(n,s.isValid(),len(s.Solids()))
        cq.exporters.export(s,str(OUT/f'{n}.step'))
        if not any(k in n for k in ('screw','insert','gasket','foam')) and n!='lens':cq.exporters.export(s,str(OUT/f'{n}.stl'),tolerance=.08,angularTolerance=.1)
        print('VALID',n,flush=True)
    allparts={**parts,**{'REF_'+n:s for n,s in refs.items()}}
    base.save_assembly(allparts,OUT/'Kiku_P1_assembly.step');base.save_assembly(parts,OUT/'Kiku_P1_housing.step')
    extended={**allparts};extended.pop('REF_FM_antenna_collapsed_BOUND');extended['REF_FM_antenna_extended_BOUND']=checks['FM_extended_outward']
    base.save_assembly(extended,OUT/'Kiku_P1_antenna_extended.step')
    for n,s in allparts.items():s.exportBrep(str(OUT/f'{n}.brep'))
    for n,s in checks.items():s.exportBrep(str(OUT/f'CHECK_{n}.brep'))
    (OUT/'part_inventory.json').write_text(json.dumps({n:{'bounds':bounds(s),'valid':s.isValid(),'solids':len(s.Solids()),'volume_mm3':s.Volume()} for n,s in allparts.items()},indent=2))
