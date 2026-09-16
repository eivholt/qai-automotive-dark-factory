"""Fit optional dense top-down R3 poses without changing inspection endpoints."""
from pathlib import Path
import hashlib
import json
import argparse
import numpy as np

parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=Path('assets/derived/welding_cell'))
root=parser.parse_args().root
scene=json.loads((root/'scene.json').read_text())
layout=json.loads((root/'panel_assembly.json').read_text())
roles=['base','shoulder','elbow','forearm_roll','wrist','tool_roll']
joints=[next(j for j in scene['joints'] if j['id']=='robot_px_ny_'+role) for role in roles]
offset=np.array(layout['holder_panel_offset_cm'])

def rotation(axis,angle):
    x,y,z=np.array(axis,dtype=float)/np.linalg.norm(axis);a=np.deg2rad(angle)
    k=np.array([[0,-z,y],[z,0,-x],[-y,x,0]])
    return np.eye(3)+np.sin(a)*k+(1-np.cos(a))*(k@k)

def fk(q):
    p=np.zeros(3);r=np.eye(3)
    for joint,angle in zip(joints,q):
        p+=r@joint['pivot_cm'];r=r@rotation(joint['axis'],angle)
    return p+r@offset,r

def ease(t):
    t=np.clip(t,0,1);return t**3*(10+t*(-15+6*t))

waypoints=[layout['holder_waypoints'][i] for i in (5,6,7)]+[layout['poses'][9]]
ends=[fk(q)[0] for q in waypoints]
times=[4.75,5.15,5.55,6.]
poses=[];errors=[];seed=np.array(waypoints[0])
for t in np.linspace(4.75,6,51):
    segment=min(2,max(0,np.searchsorted(times,t,side='right')-1))
    u=ease((t-times[segment])/(times[segment+1]-times[segment]))
    position=ends[segment]*(1-u)+ends[segment+1]*u
    tilt=.6*np.sin(np.pi*np.clip((t-5.15)/.85,0,1))**3
    target_r=rotation([0,1,0],tilt)
    def error(q):
        p,r=fk(q);return np.r_[p-position,80*(r-target_r).ravel()]
    q=seed.copy()
    for _ in range(80):
        e=error(q)
        if np.linalg.norm(e)<.0001:break
        jac=np.column_stack([(error(q+np.eye(6)[i]*.001)-e)/.001 for i in range(6)])
        q+=np.linalg.solve(jac.T@jac+.0001*np.eye(6),-jac.T@e)
    residual=float(np.linalg.norm(error(q)))
    if residual>.01 or max(abs(q))>270:raise RuntimeError(f'Unreachable lowering pose {t}: {residual}')
    poses.append(q.tolist());errors.append(residual);seed=q
# Preserve original endpoints bit-for-bit.
poses[0]=waypoints[0];poses[-1]=waypoints[-1]
result={'recipe':'dense-top-down-compliant-v1','authoring_sha256':layout['authoring_sha256'],
    'scene_sha256':hashlib.sha256((root/'scene.json').read_bytes()).hexdigest(),
    'start_seconds':4.75,'end_seconds':6.,'tilt_degrees':.6,'poses':poses,'maximum_fit_residual':max(errors)}
(root/'panel_lowering.json').write_text(json.dumps(result,indent=2)+'\n')
print('Prepared 51 lowering poses, max residual',max(errors))
