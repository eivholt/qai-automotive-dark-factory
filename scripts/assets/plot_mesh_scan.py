"""Local diagnostic projections of extracted geometry (requires numpy and Pillow)."""
import argparse, json, struct
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw, ImageFont
p=argparse.ArgumentParser();p.add_argument('scan');p.add_argument('output');p.add_argument('--robots',action='store_true');args=p.parse_args()
root=Path(args.scan);meta=json.loads((root/'islands.json').read_text()); records=[]
colors={'chrome':(166,182,192),'robots':(235,102,27),'robots_blue':(54,108,184),'robots_plastic':(73,79,91),'robots_plastic_black':(55,62,69),'robots_plastic_gray':(136,146,153),'robots_screen':(83,186,173),'robots_tiles':(70,81,84),'robots_white':(218,225,228),'robots_yellow':(238,181,45)}
for m in meta['meshes']:
    suffix=m['name'].replace('Car_Body_Welding_Robotics_Cell_','')
    raw=(root/(m['name']+'.meshbin')).read_bytes();nv,nt=struct.unpack_from('<II',raw,8)
    verts=np.frombuffer(raw,dtype='<f4',count=nv*3,offset=16).reshape(-1,3)
    dtype=np.dtype([('v','<u4',(3,)),('island','<u4'),('normals','<f4',(3,3)),('uv','<f4',(3,2))])
    tris=np.frombuffer(raw,dtype=dtype,count=nt,offset=16+nv*12)
    if args.robots:
        centers=verts[tris['v']].mean(axis=1)
        tris=tris[(np.abs(centers[:,0])<510)&(np.abs(centers[:,1])<450)&(centers[:,2]>40)]
    records.append((m,verts,tris,np.array(colors[suffix],dtype=float)))
canvas=Image.new('RGB',(2200,1800),(19,24,32));draw=ImageDraw.Draw(canvas)
views=[('TOP XY',np.array([1.,0,0]),np.array([0.,-1,0]),(0,0,1100,900)),('FRONT XZ',np.array([1.,0,0]),np.array([0.,0,1]),(1100,0,2200,900)),('SIDE YZ',np.array([0.,1,0]),np.array([0.,0,1]),(0,900,1100,1800)),('PERSPECTIVE',np.array([.8,.6,0]),np.array([-.3,.4,.8660254]),(1100,900,2200,1800))]
for title,right,up,box in views:
    forward=np.cross(right,up);arrays=[]
    for m,v,t,color in records:
        vv=v[t['v']]; xy=np.stack([vv@right,vv@up],axis=2);z=(vv@forward).mean(axis=1)
        n=np.cross(vv[:,1]-vv[:,0],vv[:,2]-vv[:,0]);lens=np.linalg.norm(n,axis=1);n=n/np.maximum(lens[:,None],1e-8)
        brightness=.48+.52*np.abs(n@np.array([.25,-.4,.88]));c=np.clip(color[None,:]*brightness[:,None],0,255).astype(np.uint8)
        arrays.append((xy,z,c))
    xy=np.concatenate([a[0] for a in arrays]);z=np.concatenate([a[1] for a in arrays]);colors_view=np.concatenate([a[2] for a in arrays]);lo=xy.min(axis=(0,1));hi=xy.max(axis=(0,1));scale=min((box[2]-box[0]-80)/(hi[0]-lo[0]),(box[3]-box[1]-90)/(hi[1]-lo[1]));origin=np.array([(box[0]+box[2])/2,(box[1]+box[3])/2]);xy=(xy-(lo+hi)/2)*np.array([scale,-scale])+origin
    # Painter ordering is only a diagnostic; actual visual QA is performed in Unreal.
    for idx in np.argsort(z):
        polygon=xy[idx]
        if max(np.ptp(polygon[:,0]),np.ptp(polygon[:,1]))<.5: continue
        draw.polygon([(float(x),float(y)) for x,y in polygon], fill=tuple(colors_view[idx]))
    draw.text((box[0]+20,box[1]+20),title,fill=(245,245,245))
    draw.text((box[0]+20,box[1]+40),f'cm bounds: {lo.round(1)} to {hi.round(1)}',fill=(170,180,195))
canvas.save(args.output)
print(args.output)