"""Read dimensional datums directly from KiCad; run using KiCad's Python."""
import json, hashlib, pathlib, pcbnew
ROOT = pathlib.Path(__file__).resolve().parents[2]
OUT = ROOT/'Mechanical/reference'
board_path = ROOT/'Walkman - Blobject.kicad_pcb'
b = pcbnew.LoadBoard(str(board_path))
def xy(p): return [round(pcbnew.ToMM(p.x),6),round(pcbnew.ToMM(p.y),6)]
data={'source':str(board_path.name),'sha256':hashlib.sha256(board_path.read_bytes()).hexdigest(),
      'coordinates':'KiCad XY in mm, Y down; mechanical X=Xpcb-72.11, Y=157.90-Ypcb; Z=0 board back, Z=1.6 front',
      'board_thickness':pcbnew.ToMM(b.GetDesignSettings().GetBoardThickness()),'footprints':[],'drawings':[]}
for f in b.GetFootprints():
    d={'ref':f.GetReference(),'value':f.GetValue(),'footprint':str(f.GetFPID().GetLibItemName()),'xy':xy(f.GetPosition()),'angle':f.GetOrientationDegrees(),'layer':f.GetLayerName(),'models':[],'pads':[]}
    for m in f.Models():
        d['models'].append({'path':m.m_Filename,'offset':[m.m_Offset.x,m.m_Offset.y,m.m_Offset.z], 'rotation':[m.m_Rotation.x,m.m_Rotation.y,m.m_Rotation.z],'scale':[m.m_Scale.x,m.m_Scale.y,m.m_Scale.z]})
    for p in f.Pads():
        d['pads'].append({'number':p.GetNumber(),'xy':xy(p.GetPosition()),'size':xy(p.GetSize()),'drill':xy(p.GetDrillSize()),'net':p.GetNetname()})
    data['footprints'].append(d)
for g in b.GetDrawings():
    if isinstance(g,pcbnew.PCB_SHAPE):
        d={'layer':g.GetLayerName(),'shape':g.GetShapeStr(),'start':xy(g.GetStart()),'end':xy(g.GetEnd())}
        if g.GetShape()==pcbnew.SHAPE_T_ARC: d['mid']=xy(g.GetArcMid())
        data['drawings'].append(d)
(OUT/'board_datums.json').write_text(json.dumps(data,indent=2))
for f in data['footprints']:
    if f['ref'].startswith(('J','SW','H','BT','LS','FPC','U1')):
        print(f['ref'],f['value'],f['xy'],f['angle'],f['models'])
