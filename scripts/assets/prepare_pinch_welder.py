"""Separate the source gun's two hinged electrode assemblies for process mode.

Auxiliary export only: preserve the original assignment table, whole gun and
legacy scene. Every triangle of the copied gun belongs to exactly one piece.
Units are source-prepared centimetres; this is an authored visual mechanism.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from prepare_welding_cell import read_mesh, rotation_matrix


def prepare(root):
    groups=json.loads((root/'grouping_report.json').read_text())['groups']
    gun=next(g for g in groups if g['id']=='SM_robot_nx_py_tool')
    origin=np.array(gun['pivot_cm'])
    cache={}; records=[]
    for member in gun['islands']:
        key=member['mesh']
        if key not in cache: cache[key]=read_mesh(root/'scan'/(key+'.meshbin'))
        vertices,triangles,_=cache[key]
        faces=triangles[triangles['island']==member['island']]
        points=vertices[np.unique(faces['v'])].astype(float)-origin
        records.append(dict(mesh=key,island=member['island'],points=points,triangles=len(faces)))
    # Visible bearing-pin pairs at the top of the long copper arms. Keep their
    # fixed outer forks with the chassis; rotate the pins/lower clevis and arms.
    def center(island):
        return next(r['points'] for r in records if r['island']==island).mean(0)
    pivots=np.array([(center(316)+center(320))*.5,(center(326)+center(327))*.5])
    axis=((center(320)-center(316))+(center(327)-center(326)))*.5
    axis/=np.linalg.norm(axis)
    pinch=pivots[1]-pivots[0];pinch-=axis*np.dot(pinch,axis);pinch/=np.linalg.norm(pinch)
    split=pivots.mean(0)
    parts=[[],[],[]]
    for record in records:
        center_point=record['points'].mean(0)
        # Source islands already separate the bearing pins, lower clevis,
        # cylinders, collars, elbow pipes and contact caps from the outer fork.
        group=0 if center_point[2]>=-48. else 1+int(np.dot(center_point-split,pinch)>0)
        record['part']=group;parts[group].append(record)
    tips=[]
    for index in range(2):
        points=np.concatenate([r['points'] for r in parts[index+1]])
        points=points[points[:,2]<-90.]
        distances=(points-split)@pinch
        edge=distances.max() if index==0 else distances.min()
        face=points[np.abs(distances-edge)<.03]
        if len(face)<3: raise ValueError('Contact cap plane not found')
        tips.append(face.mean(0))
    tips=np.array(tips)
    def moved(angle):
        return np.array([pivots[i]+rotation_matrix(axis,(-1 if i==0 else 1)*angle)@(tips[i]-pivots[i]) for i in range(2)])
    def gap(angle): return np.dot(moved(angle)[1]-moved(angle)[0],pinch)*.7
    if gap(0)>1.2 or gap(5)<1.2: raise ValueError('Unexpected source electrode gap')
    lo,hi=0.,5.
    for _ in range(50):
        mid=(lo+hi)*.5
        if gap(mid)<1.2: lo=mid
        else: hi=mid
    clamp=(lo+hi)*.5
    names=['SM_ProcessPinchChassis','SM_ProcessPinchJawA','SM_ProcessPinchJawB']
    table=(root/'assignments.tsv').read_bytes(); replacement={(r['mesh'],str(r['island'])):names[r['part']] for r in records}
    rewritten=[];count=0
    for line in table.decode().splitlines():
        fields=line.split()
        if len(fields)==6 and (fields[0],fields[1]) in replacement:
            fields[2]=replacement[fields[0],fields[1]];fields[3:]=[format(v,'.12g') for v in origin]
            line='\t'.join(fields);count+=1
        rewritten.append(line)
    if count!=len(records) or sum(r['triangles'] for r in records)!=gun['triangles']:
        raise ValueError('Gun source-island conservation failed')
    (root/'pinch_gun_assignments.tsv').write_text('\n'.join(rewritten)+'\n')
    (root/'pinch_gun_groups.txt').write_text('\n'.join(names)+'\n')
    manifest={'version':1,'fbx':'pinch_gun.fbx','meshes':[]}
    for name,part in zip(names,parts):
        points=np.concatenate([r['points'] for r in part])
        manifest['meshes'].append({'id':name,'fbx_node':name,'expected_bounds_min_cm':points.min(0).tolist(),'expected_bounds_max_cm':points.max(0).tolist()})
    descriptor={'recipe':'source-two-hinge-pinch-v1','source_component':gun['id'],'meshes':names,
        'hinge_pivots_cm':pivots.tolist(),'hinge_axis':axis.tolist(),'source_tip_centers_cm':tips.tolist(),
        'closed_tip_centers_cm':moved(clamp).tolist(),'tcp_cm':moved(clamp).mean(0).tolist(),
        'pinch_axis':pinch.tolist(),'clamp_angle_deg':clamp,'open_angle_deg':14.,'travel_angle_deg':7.,
        'clamp_gap_cm':gap(clamp),'open_gap_cm':gap(14.),'source_triangles':gun['triangles'],
        'parts':[{'name':name,'triangles':sum(r['triangles'] for r in part),'islands':[{'mesh':r['mesh'],'island':r['island']} for r in part]} for name,part in zip(names,parts)],
        'source_assignments_sha256':hashlib.sha256(table).hexdigest(),
        'authoring_sha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        'scope':'Copied source triangles; illustrative pin-jointed clamp motion, not calibrated resistance-welder kinematics'}
    if table!=(root/'assignments.tsv').read_bytes(): raise ValueError('Original assignments changed')
    for name,data in [('pinch_gun_manifest.json',manifest),('pinch_gun.json',descriptor)]:
        (root/name).write_text(json.dumps(data,indent=2)+'\n')
    return {k:descriptor[k] for k in ['recipe','clamp_angle_deg','clamp_gap_cm','open_gap_cm','source_triangles','tcp_cm','hinge_pivots_cm','pinch_axis']}


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=Path('assets/derived/welding_cell'))
    print(json.dumps(prepare(parser.parse_args().root),indent=2))
