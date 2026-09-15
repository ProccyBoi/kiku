"""P0 offline orthographic CAD review renders, directly from tessellated BREP."""
import pathlib,sys,json
import vtk
from PIL import Image,ImageDraw,ImageFont
ROOT=pathlib.Path(__file__).resolve().parents[2]/'Mechanical'
COL={'front_shell':(.85,.87,.75),'rear_shell':(.29,.39,.34),'lens':(.065,.115,.12),'button_left':(.94,.49,.20),'button_right':(.80,.82,.65),'encoder_knob':(.20,.32,.25),'power_button':(.29,.39,.34)}
def render(files,path,view='iso',size=(1100,1250),explode=False,section=None):
    ren=vtk.vtkRenderer();ren.SetBackground(.945,.941,.914)
    for name,fn in files:
        if fn.suffix=='.stl':
            rd=vtk.vtkSTLReader();rd.SetFileName(str(fn));rd.Update();poly=rd.GetOutput()
        else:
            import cadquery as cq
            s=cq.Shape.importBrep(str(fn));verts,faces=s.tessellate(.16,.2)
            pts=vtk.vtkPoints()
            for p in verts:pts.InsertNextPoint(p.x,p.y,p.z)
            cells=vtk.vtkCellArray()
            for tri in faces:
                cells.InsertNextCell(3)
                for ix in tri:cells.InsertCellPoint(ix)
            poly=vtk.vtkPolyData();poly.SetPoints(pts);poly.SetPolys(cells)
        if section:
            # Sew coincident tessellation vertices before asking VTK to cap a cut.
            # Per-face CAD tessellation duplicates seam vertices otherwise.
            welded=vtk.vtkCleanPolyData();welded.SetInputData(poly);welded.Update();poly=welded.GetOutput()
            plane=vtk.vtkPlane();plane.SetOrigin(*section[0]);plane.SetNormal(*section[1])
            clip=vtk.vtkClipClosedSurface();pc=vtk.vtkPlaneCollection();pc.AddItem(plane)
            clip.SetClippingPlanes(pc);clip.SetInputData(poly);clip.Update();poly=clip.GetOutput()
        clean=vtk.vtkCleanPolyData();clean.SetInputData(poly);clean.Update()
        normals=vtk.vtkPolyDataNormals();normals.SetInputConnection(clean.GetOutputPort());normals.SetFeatureAngle(45);normals.Update()
        mapper=vtk.vtkPolyDataMapper();mapper.SetInputConnection(normals.GetOutputPort());mapper.ScalarVisibilityOff()
        actor=vtk.vtkActor();actor.SetMapper(mapper)
        col=COL.get(name,(.6,.63,.6))
        if name.startswith('REF_'):col=(.12,.36,.24) if name=='REF_PCB' else (.48,.5,.52)
        if 'BATTERY' in name:col=(.50,.63,.77)
        if 'LCD' in name:col=(.20,.28,.31)
        if 'screw' in name or 'insert' in name:col=(.55,.50,.37)
        actor.GetProperty().SetColor(*col);actor.GetProperty().SetAmbient(.3);actor.GetProperty().SetDiffuse(.7);actor.GetProperty().SetSpecular(.24);actor.GetProperty().SetSpecularPower(40)
        if explode:
            dz=0
            if name=='rear_shell':dz=-42
            elif name in ('front_shell','display_carrier'):dz=38
            elif name=='lens' or name.startswith('button') or name=='encoder_knob':dz=58
            elif name=='battery_guard':dz=-15
            elif 'BATTERY' in name or 'SPEAKER' in name:dz=-25
            actor.SetPosition(0,0,dz)
        ren.AddActor(actor)
    camera=ren.GetActiveCamera();center=(30,53,0)
    dirs={'front':(0,0,1),'rear':(0,0,-1),'side':(1,0,0),'iso':(1,-1,1.65),'inside_front':(-.4,-.5,-1),'inside_rear':(.5,-.6,1)}
    d=dirs[view];camera.SetFocalPoint(*center);camera.SetPosition(*(center[i]+250*d[i] for i in range(3)));camera.SetViewUp(0,1,0);camera.ParallelProjectionOn();camera.SetParallelScale(72 if not explode else 104)
    win=vtk.vtkRenderWindow();win.SetOffScreenRendering(1);win.SetSize(*size);win.AddRenderer(ren);win.SetMultiSamples(8)
    ren.ResetCameraClippingRange();win.Render()
    cap=vtk.vtkWindowToImageFilter();cap.SetInput(win);cap.ReadFrontBufferOff();cap.Update()
    wr=vtk.vtkPNGWriter();wr.SetFileName(str(path));wr.SetInputConnection(cap.GetOutputPort());wr.Write();win.Finalize()
def font(s,bold=False):return ImageFont.truetype('C:/Windows/Fonts/'+('segoeuib.ttf' if bold else 'segoeui.ttf'),s)
def concept_sheets():
    names=['01_Mame','02_Nagare','03_Mini'];subtitles=['Soft pebble / balanced grip','Offset swell / expressive silhouette','Compact capsule / MiniDisc restraint']
    sheet=Image.new('RGB',(2400,2220),'#f1f0e9');d=ImageDraw.Draw(sheet)
    d.text((65,38),'kiku   /   form studies',font=font(52,True),fill='#26392f')
    d.text((65,108),'Same KiCad datums in all three concepts. Exterior studies; internal engineering follows in Mame P0.',font=font(24),fill='#596457')
    for row,(name,sub) in enumerate(zip(names,subtitles)):
        files=[(p.stem[len(name)+1:],p) for p in (ROOT/'concepts').glob(name+'_*.stl')];y=170+row*675
        d.text((65,y),name.replace('_','  ')+'   /   '+sub,font=font(29,True),fill='#26392f')
        for col,view in enumerate(['front','rear','side','iso']):
            path=ROOT/f'concepts/{name}_{view}.png';render(files,path,view,size=(570,595))
            sheet.paste(Image.open(path),(55+col*585,y+50));d.text((70+col*585,y+628),view.upper(),font=font(18),fill='#596457')
    sheet.save(ROOT/'concepts/three_concepts.png')
def final_views():
    inventory=json.loads((ROOT/'exports/P0/part_inventory.json').read_text())
    files=[(p.stem,p) for p in (ROOT/'exports/P0').glob('*.brep') if p.stem in inventory]
    outer=[p for p in files if p[0] in COL]
    if '--sections' not in sys.argv:
        for view in ('front','rear','side','iso'):render(outer,ROOT/f'review/P0_assembled_{view}.png',view)
        render(files,ROOT/'review/P0_exploded.png','iso',size=(1600,1600),explode=True)
    render(files,ROOT/'review/P0_section_longitudinal.png','side',size=(1300,1500),section=((30,0,0),(-1,0,0)))
    render(files,ROOT/'review/P0_section_controls.png','iso',size=(1500,1500),section=((0,55,0),(0,1,0)))
    render(files,ROOT/'review/P0_section_speaker.png','side',size=(1300,1500),section=((18,0,0),(-1,0,0)))
    render([p for p in files if p[0]=='front_shell'],ROOT/'review/P0_front_inside.png','inside_front')
    render([p for p in files if p[0]=='rear_shell'],ROOT/'review/P0_rear_inside.png','inside_rear')
if __name__=='__main__':
    if '--concepts' in sys.argv:concept_sheets()
    else:final_views()
