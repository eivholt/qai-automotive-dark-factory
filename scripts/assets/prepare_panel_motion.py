"""Author source-bound six-joint panel assembly and fixed-camera endpoints.

This is bounded visual FK/IK, not a calibrated industrial motion planner.
Requires NumPy and the locally prepared purchased-cell scan; writes only the
ignored derived layout consumed by InitializePanelAssembly().
"""
import hashlib,json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,'scripts/assets')
from prepare_welding_cell import rotation_matrix,read_mesh
import argparse
parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=Path('assets/derived/welding_cell'))
parser.add_argument('--output',type=Path,help='Optional private candidate descriptor for geometry audits before promotion.')
args=parser.parse_args();root=args.root
scene=json.loads((root/'scene.json').read_text())
roles=['base','shoulder','elbow','forearm_roll','wrist','tool_roll'];ids=['robot_px_py','robot_nx_py','robot_px_ny','robot_nx_ny']
joints=[[next(j for j in scene['joints'] if j['id']==r+'_'+k) for k in roles] for r in ids]
# Exchange the left stations' functions while retaining logical R2/R4 identities.
# The rear welder sits50cm inward to reach the far contact with a direct gun.
base_targets=[np.array([240.,-100.,175.584213]),np.array([-301.093104,-294.770226,175.584213]),np.array(joints[2][0]['pivot_cm']),np.array([-240.,-100.,175.584213])]
offsets=[base_targets[i]-np.array(joints[i][0]['pivot_cm']) for i in range(4)]
def fk(i,q):
 p=offsets[i].copy();r=np.eye(3)
 for j,a in zip(joints[i],q):
  p+=r@j['pivot_cm'];r=r@rotation_matrix(np.array(j['axis']),a)
 return p,r

def solve(i,p,r,seed):
 def err(q):
  pp,rr=fk(i,q)
  return np.r_[pp-p,80*(rr-r).ravel()]
 best=None
 for k in range(8):
  q=np.array(seed,dtype=float)+(np.random.default_rng(k).normal(0,25,6) if k else 0)
  for _ in range(250):
   e=err(q)
   if np.linalg.norm(e)<.002:break
   jac=np.column_stack([(err(q+np.eye(6)[a]*.002)-e)/.002 for a in range(6)])
   dq=np.linalg.solve(jac.T@jac+.003*np.eye(6),-jac.T@e)
   dq*=min(1,6/max(np.max(np.abs(dq)),1e-8));q+=dq
   q=np.clip(q,-270,270)
  score=np.linalg.norm(err(q))
  if best is None or score<best[0]:best=(score,q.copy())
  if score<.005:break
 if best[0]>.05: raise ValueError(f'Unreachable endpoint robot {i}: residual {best[0]}')
 return best[1].tolist(),best[0]

v,t,_=read_mesh(root/'scan/Car_Body_Welding_Robotics_Cell_chrome.meshbin')
body=v[t['v'][t['island']==386]].astype(float)
def contact(x,y):
 a=body[:,0];u=body[:,1]-a;vv=body[:,2]-a;den=u[:,0]*vv[:,1]-u[:,1]*vv[:,0]
 safe=np.where(abs(den)>1e-8,den,1);b=((x-a[:,0])*vv[:,1]-(y-a[:,1])*vv[:,0])/safe;c=(u[:,0]*(y-a[:,1])-u[:,1]*(x-a[:,0]))/safe
 okay=(abs(den)>1e-8)&(b>=0)&(c>=0)&(b+c<=1);z=a[:,2]+b*u[:,2]+c*vv[:,2];z[~okay]=-np.inf;index=np.argmax(z)
 if not np.isfinite(z[index]):raise ValueError(f'No contact at {x},{y}')
 n=np.cross(u[index],vv[index]);n/=np.linalg.norm(n)
 if n[2]<0:n=-n
 return [x,y,z[index]],n.tolist()
contacts=[contact(x,y) for x,y in [(80,-235),(-80,-235),(80,-235)]]
def align_z(normal):
 n=np.array(normal,dtype=float);n/=np.linalg.norm(n);v=np.cross([0.,0.,1.],n)
 k=np.array([[0,-v[2],v[1]],[v[2],0,-v[0]],[-v[1],v[0],0]])
 return np.eye(3)+k+k@k/(1+n[2])

# Two rounded runs cover the front contact and both sides; the rear edge is free.
# Their centerlines are source-surface fitted, not floating rectangular decals.
adhesive_paths=[]
for bead in range(2):
 controls=np.array([[3,-235],[95,-235],[99,-242],[106,-277],[112,-312],[114,-323]],float)
 if bead: controls[:,0]=-controls[:,0]+3.2;controls=controls[::-1]
 # Rounded corners via quadratic Bezier blends, then equal-distance sampling.
 dense=[controls[0]]
 for k in range(1,len(controls)-1):
  a,b,c=controls[k-1:k+2];enter=b+(a-b)*min(.15,3/np.linalg.norm(a-b));leave=b+(c-b)*min(.15,3/np.linalg.norm(c-b))
  dense.extend([enter])
  dense.extend([(1-u)**2*enter+2*u*(1-u)*b+u*u*leave for u in np.linspace(0,1,17)[1:]])
 dense.append(controls[-1]);dense=np.array(dense);arc=np.r_[0,np.cumsum(np.linalg.norm(np.diff(dense,axis=0),axis=1))]
 points=[]
 for d in np.linspace(0,arc[-1],41):
  x,y=[np.interp(d,arc,dense[:,k]) for k in range(2)]
  pp,_=contact(x,y);points.append(np.array(pp))
 points=np.array(points)
 tangent=np.gradient(points,axis=0);tangent/=np.linalg.norm(tangent,axis=1)[:,None]
 side=np.cross(np.array([0.,0.,1.]),tangent);side/=np.linalg.norm(side,axis=1)[:,None]
 normals=np.cross(tangent,side);distance=np.r_[0,np.cumsum(np.linalg.norm(np.diff(points,axis=0),axis=1))]
 adhesive_paths.append({'points_cm':points.tolist(),'normals':normals.tolist(),'progress':(distance/distance[-1]).tolist(),
  'start_seconds':1.25 if bead==0 else 6.80,'end_seconds':6.00 if bead==0 else 11.55,'length_cm':float(distance[-1])})
# R2 welds from the opposite side of R3; R1 applies adhesive before pickup.
# Seeds select the audited visual IK branch; contact positions remain source-fitted.
work_seeds = [[-10.7677, -2.4896, 4.9821, 10.6215, 14.4909, -163.2488], [-25.0607, 29.2053, -31.4913, -0.9929, 1.7873, -155.42], [-33.5838, 0.3157, 0.7899, 18.0456, 10.499, -136.6221], [50.7766, 27.8177, -19.0719, 7.736, -6.7011, 133.813], [38.4044, -4.4401, 27.0024, 5.7989, -21.1246, 142.3933], [53.7866, -12.4529, 40.9458, 7.9273, -25.7726, 126.2512]]
# Scale and roll the purchased gun about its measured native attachment cap.
# This preserves cap center/plane at the flange without an extension rod.
gun_cap=np.array([.0518951036,.0332041518,-6.8859544067])
gun_cap_normal=np.array([-.00753606888,-.00482181859,.99995997806]);gun_cap_normal/=np.linalg.norm(gun_cap_normal)
gun_mount_r=rotation_matrix(gun_cap_normal,-90.)
gun_mount_q=np.r_[gun_cap_normal*np.sin(-np.pi/4),np.cos(-np.pi/4)]
gun_mount_t=gun_cap-gun_mount_r@gun_cap*.7
welder_tip_local=gun_mount_r@np.array(scene['cycle']['emitters'][1]['location_cm'])*.7+gun_mount_t
welder_direction_local=gun_mount_r@np.array(scene['cycle']['emitters'][1]['direction']);welder_direction_local/=np.linalg.norm(welder_direction_local)
# Aim the measured electrode along each surface normal using physical joints.
vdir=np.cross(welder_direction_local,[0.,0.,-1.]);k=np.array([[0,-vdir[2],vdir[1]],[vdir[2],0,-vdir[0]],[-vdir[1],vdir[0],0]])
welder_flange_align=np.eye(3)+k+k@k/(1-welder_direction_local[2])
weld_yaw_deg=[315.,315.,315.]
poses=[]
for role_index,i in enumerate([1,0]):
 tip=welder_tip_local if role_index==0 else np.array(scene['cycle']['emitters'][i]['location_cm'])
 for contact_index,(p,n) in enumerate(contacts):
  normal=np.array(n);tangent=np.cross(normal,[0.,1.,0.]);tangent/=np.linalg.norm(tangent)
  if contact_index==1:tangent=-tangent
  target=np.array(p)+normal*.6 if role_index==0 else np.array(p)+tangent*22.+normal*2.9
  tilt=0 if role_index==0 and contact_index==1 else 20
  r=align_z(normal)@rotation_matrix(np.array([0,0,1]),weld_yaw_deg[contact_index])@welder_flange_align if role_index==0 else rotation_matrix(np.array([0,0,1]),180)@rotation_matrix(np.array([0,1,0]),tilt)
  q,e=solve(i,target-r@tip,r,work_seeds[role_index*3+contact_index]);poses.append(q)
# Waypoints lift before turning and go around the stationary vacuum rods.
r180=rotation_matrix(np.array([0,0,1]),180);r20=r180@rotation_matrix(np.array([0,1,0]),20)
weld_front_r=rotation_matrix(np.array([0,0,1]),weld_yaw_deg[0])@welder_flange_align
weld_rear_r=rotation_matrix(np.array([0,0,1]),weld_yaw_deg[2])@welder_flange_align
w_hover,_=solve(1,np.array(contacts[1][0])+np.array(contacts[1][1])*.6+[0,0,60]-weld_front_r@welder_tip_local,weld_front_r,poses[1])
w_rear,_=solve(1,np.array([-90,-365,290])-weld_rear_r@welder_tip_local,weld_rear_r,poses[2])
welder_waypoints=[[-90,65,-35,0,0,0],w_hover,w_rear]
# Straight Cartesian travel keeps the restored gun clear of the holder bars;
# two-endpoint joint interpolation otherwise bows back through the gripper.
weld_cross_poses=[];seed=poses[0]
for u in np.linspace(0,1,21):
 target=np.array(contacts[0][0])*(1-u)+np.array(contacts[1][0])*u+[0,0,.6]
 seed,e=solve(1,target-weld_front_r@welder_tip_local,weld_front_r,seed);weld_cross_poses.append(seed)
weld_cross_poses[0]=poses[0];weld_cross_poses[-1]=poses[1]
# Continuous dispensing strokes: constant arc-length parameter shared with
# the visible GPU bead reveal; smoothstep timing ramps flow at each end.
def sealant_frame(pp,n):
 radial=np.array(pp)[:2]-base_targets[0][:2]
 yaw=np.degrees(np.arctan2(radial[1],radial[0]))
 return align_z(n)@rotation_matrix(np.array([0,0,1]),yaw)@rotation_matrix(np.array([0,1,0]),-30)
adhesive_stroke_poses=[];tip=np.array(scene['cycle']['emitters'][0]['location_cm']);seed=poses[3]
for path in adhesive_paths:
 for pp,n in zip(path['points_cm'],path['normals']):
  rr=sealant_frame(pp,n);target=np.array(pp)+np.array(n)*2.
  seed,e=solve(0,target-rr@tip,rr,seed);adhesive_stroke_poses.append(seed)
hovers=[]
for flat_index in [0,40,41,81]:
 bead,index=divmod(flat_index,41);path=adhesive_paths[bead];pp=np.array(path['points_cm'][index]);n=np.array(path['normals'][index]);rr=sealant_frame(pp,n)
 q,e=solve(0,pp+n*2.+[0,0,40]-rr@tip,rr,adhesive_stroke_poses[flat_index]);hovers.append(q)
# Rotate only the process dispenser wrist at rest: its reservoir clears the
# source base casting while every adhesive work and inspection pose is retained.
adhesive_rest_pose=[j['rest_angle_deg'] for j in joints[0]]
adhesive_rest_pose[4]+=20.
adhesive_waypoints=[[-180,65,0,0,0,0],[hovers[0][0],65,0,0,0,0],hovers[0],hovers[1],hovers[2],hovers[3],[hovers[3][0],65,0,0,0,0]]
# Holder poses 6: pickup,7: lift,8: over-car,9: seated. Shorten only the
# flange-to-frame stem by 25%; preserve the frame/pads and panel world targets.
holder_stem_length_cm=122.*.75
holder_offset_cm=holder_stem_length_cm+28.
# Grip to the right of panel center, leaving the direct welder a clear approach.
holder_grip_offset=np.array([65.,0.,holder_offset_cm])
# Fixed panel mount orientation remains identity.
panel_seating_lift_cm=.5
panelcenter=np.array([1.606224,-285.107239,269.289307+panel_seating_lift_cm])
# Slot4 center and source-aligned vertical panel rotation.
stackpoints=v[np.unique(t['v'][t['island']==555])].astype(float);bp=body.reshape(-1,3)
_,_,ub=np.linalg.svd(bp-bp.mean(axis=0),full_matrices=False);_,_,us=np.linalg.svd(stackpoints-stackpoints.mean(axis=0),full_matrices=False)
# Brute sign choices preserve determinant +1 and find intended near-cyclic orientation.
ideal=np.array([[0,0,1],[1,0,0],[0,1,0]],dtype=float);cand=[]
for x in [-1,1]:
 for y in [-1,1]:
  for z in [-1,1]:
   rr=us.T@np.diag([x,y,z])@ub
   if np.linalg.det(rr)>.9:cand.append(rr)
stackR=min(cand,key=lambda rr:np.linalg.norm(rr-ideal))
# Move the complete rack and stock 45 cm outward and 70 cm rearward from the
# original process position, between R3's base and the adjacent control cabinet.
stackDelta=np.array([1652.2,-836.94,0.]) # rack center approximately515,-510
stackCenter=(stackpoints.min(axis=0)+stackpoints.max(axis=0))/2+stackDelta
seed=[180,0,-20,0,90,0]
for ki,(p,r) in enumerate([(stackCenter+stackR@holder_grip_offset,stackR),
             (stackCenter+stackR@holder_grip_offset+[0,0,190],stackR),
             (panelcenter+holder_grip_offset+[0,0,5],np.eye(3)),(panelcenter+holder_grip_offset,np.eye(3))]):
 seed=[[183,-58,26,-13,-48,-98],[183,-25,16,-17,-71,-92],[11,-20,52,0,-32,-11],[11,-16,29,0,-13,-11]][ki]
 q,e=solve(2,np.array(p),r,seed);poses.append(q)
# Alternative pickup slots retain rigid mount and exact rack spacing.
for slot in range(4):
 vv=v[np.unique(t['v'][t['island']==551+slot])].astype(float)
 cc=(vv.min(axis=0)+vv.max(axis=0))/2+stackDelta
 for height in [0,190]:
  pp=cc+stackR@holder_grip_offset+[0,0,height]
  q,e=solve(2,pp,stackR,poses[6+(height>0)]);poses.append(q)
# Lift clear of the rack, swing behind the body, then flatten the panel above
# the permanent mounting frame. All final insertion motion lowers from above.
# This remains authored visual choreography, not industrial motion planning.
sideR=rotation_matrix(np.array([0,0,1]),90)
holder_specs=[([400,-308,340],stackR),([350,-560,350],np.eye(3)),
    ([180,-490,360],np.eye(3)),([45,-400,360],np.eye(3)),
    (panelcenter+[0,0,90],np.eye(3)),(panelcenter+[0,0,35],np.eye(3)),
    (panelcenter+[0,0,8],np.eye(3))]
holder_waypoints=[];seed=poses[7]
for waypoint_index,(c,r) in enumerate(holder_specs):
 q,e=solve(2,np.array(c)+r@holder_grip_offset,r,seed);holder_waypoints.append(q);seed=q
 if waypoint_index==0:
  turned=q.copy();turned[0]=104.8297001114;holder_waypoints.append(turned)
release,e=solve(2,panelcenter+holder_grip_offset+[0,0,90],np.eye(3),poses[9]);holder_waypoints.append(release)
# Preserve the stock-clearing process park: its flange sits100 cm above the
# original rest flange. The shorter stem retracts the frame toward this flange.
source_rest=[j['rest_angle_deg'] for j in joints[2]];rest_p,rest_r=fk(2,source_rest)
# Unfold the process parked wrist45degrees so the shorter frame and pads sit
# outside the upper-arm casting; preserve every pickup/seat endpoint.
parked,e=solve(2,rest_p+[0,0,100],rest_r,source_rest);parked[4]+=45.;holder_waypoints.append(parked)
# New fixed-mount camera locations view same panel from above; no gimbal.
# One fixed physical camera mount, tilted60deg around localY, retains all
# original world lens poses while making them reachable from the front station.
qmount=np.array([.31880874370107315,.6421851891748259,.5308297780318088,.45186160989725177]);x,y,z,w=qmount
mountR=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
lenslocal=np.array([0,0,-24])+mountR@[21,0,0]
cam=[];seed=[.2,19.4,9,0,-.94,0]
for loc,target in [(np.array([-100.,-210,425]),np.array([80.,-235,270.])),(np.array([-150.,-305,445]),np.array([-80.,-235,270.])),(np.array([-130.,-370,445]),np.array([0.,-326,270.]))]:
 f=(target-loc);f/=np.linalg.norm(f);right=np.cross([0,0,1],f);right/=np.linalg.norm(right);up=np.cross(f,right);cr=np.column_stack([f,right,up]);r=cr@mountR.T;p=loc-r@lenslocal
 q,e=solve(3,p,r,seed);cam.append(q);seed=q
nir=[]
for k,loc in enumerate([np.array([-230.,-235,520]),np.array([-220.,-310,535]),np.array([-260.,-390,520])]):
 f=panelcenter-loc;f/=np.linalg.norm(f);right=np.cross([0,0,1],f);right/=np.linalg.norm(right);up=np.cross(f,right)
 cr=np.column_stack([f,right,up]);r=cr@mountR.T;p=loc-r@lenslocal
 q,e=solve(3,p,r,cam[k]);nir.append(q)
# Three rigid views cover entire right/left/right contact runs, including sides.
adhesive_cam=[]
for k,loc in enumerate([np.array([-100.,-210.,440.]),np.array([-80.,-290.,450.]),np.array([-95.,-365.,465.])]):
 target=np.mean(adhesive_paths[1 if k==1 else 0]['points_cm'],axis=0)
 f=target-loc;f/=np.linalg.norm(f);right=np.cross([0,0,1],f);right/=np.linalg.norm(right);up=np.cross(f,right)
 cr=np.column_stack([f,right,up]);rr=cr@mountR.T;pp=loc-rr@lenslocal
 q,e=solve(3,pp,rr,cam[k]);adhesive_cam.append(q)
# Opposed electrodes clamp across the sheet normal; the gun throat approaches
# horizontally from the front of the body. No extension/spacer is introduced.
pinch=json.loads((root/'pinch_gun.json').read_text())
source_tcp=np.array(pinch['tcp_cm']);source_pinch=np.array(pinch['pinch_axis'])
long_axis=source_tcp-gun_cap;long_axis-=source_pinch*np.dot(long_axis,source_pinch);long_axis/=np.linalg.norm(long_axis)
source_basis=np.column_stack([source_pinch,long_axis,np.cross(source_pinch,long_axis)])
world_normal=np.array([0.,0.,1.]);world_long=np.array([1.,-1.,0.])/np.sqrt(2.)
whole_gun_r=np.column_stack([world_normal,world_long,np.cross(world_normal,world_long)])@source_basis.T
weld_front_r=whole_gun_r@gun_mount_r.T
welder_tip_local=gun_mount_r@source_tcp*.7+gun_mount_t
welder_direction_local=gun_mount_r@source_pinch
# Six exposed welds sit on the two supported front contact flanges, three
# per side, 6 cm behind the bead centers on exposed source steel. The lower
# flange wings support this row without covering its thermal surface.
# There is no rear-middle weld or generated rear support.
contacts=[contact(x,-241.) for x in [-70.,-45.,-20.,20.,45.,70.]]
contacts=[((np.array(p)+[0,0,panel_seating_lift_cm]).tolist(),n) for p,n in contacts]
pinch_yaws=[0.,0.,0.,15.,30.,45.]
def pinch_frame(nn,yaw):
    n=np.array(nn);theta=np.radians(yaw);long=np.array([np.sin(theta),-np.cos(theta),0.])
    long-=n*np.dot(n,long);long/=np.linalg.norm(long)
    return np.column_stack([n,long,np.cross(n,long)])@source_basis.T@gun_mount_r.T
weld_poses=[];seed=poses[1]
for wi,(pp,nn) in enumerate(contacts):
    rr=pinch_frame(nn,pinch_yaws[wi])
    seed,_=solve(1,np.array(pp)+np.array(nn)*(-.55)-rr@welder_tip_local,rr,seed)
    weld_poses.append(seed)
weld_cross_poses=[]
for segment in range(5):
    p0,n0=map(np.asarray,contacts[segment]);p1,n1=map(np.asarray,contacts[segment+1])
    seed=weld_poses[segment]
    for u in np.linspace(0,1,11):
        if segment and u==0:continue
        nn=(1-u)*n0+u*n1;nn/=np.linalg.norm(nn);rr=pinch_frame(nn,(1-u)*pinch_yaws[segment]+u*pinch_yaws[segment+1])
        target=(1-u)*p0+u*p1+nn*(-.55)+np.array([0.,28.*np.sin(np.pi*u),3.*np.sin(np.pi*u)])
        seed,_=solve(1,target-rr@welder_tip_local,rr,seed);weld_cross_poses.append(seed)
for i,q in enumerate(weld_poses):weld_cross_poses[i*10]=q
weld_hovers=[]
for i in [0,5]:
    pp,nn=map(np.asarray,contacts[i]);rr=pinch_frame(nn,pinch_yaws[i])
    q,_=solve(1,pp+nn*(-.55)+[0,32,2]-rr@welder_tip_local,rr,weld_poses[i]);weld_hovers.append(q)
welder_waypoints=[[-150,65,-35,0,0,0],*weld_hovers]
# Each rigid thermal pose covers a pair of welds. NIR and adhesive views above
# retain their original physical targets and three-image processing.
cam=[]
for k,loc in enumerate([np.array([-150.,-305.,445.]),np.array([-130.,-350.,460.]),np.array([-100.,-210.,425.])]):
    target=np.mean([contacts[2*k][0],contacts[2*k+1][0]],axis=0)
    f=target-loc;f/=np.linalg.norm(f);right=np.cross([0.,0.,1.],f);right/=np.linalg.norm(right);up=np.cross(f,right)
    cr=np.column_stack([f,right,up]);rr=cr@mountR.T
    q,_=solve(3,loc-rr@lenslocal,rr,adhesive_cam[min(k,2)]);cam.append(q)
out={'schema':'panel-assembly-authored-poses-v1','robot_ids':ids,'joint_roles':roles,'base_offsets_cm':[o.tolist() for o in offsets],
'contacts':[{'position_cm':p,'normal':n}for p,n in contacts], 'poses':poses,'welder_waypoints':welder_waypoints,'weld_cross_poses':weld_cross_poses,'weld_surface_yaw_deg':weld_yaw_deg,'adhesive_waypoints':adhesive_waypoints,'adhesive_rest_pose':adhesive_rest_pose,'adhesive_paths':adhesive_paths,'adhesive_stroke_poses':adhesive_stroke_poses,'holder_waypoints':holder_waypoints,'holder_offset_cm':holder_offset_cm,'holder_stem_length_cm':holder_stem_length_cm,'motion_recipe':'top-down-frame-pinch-paths-v11','camera_poses':cam,'nir_camera_poses':nir,'adhesive_inspection_poses':adhesive_cam,
'supply_delta_cm':stackDelta.tolist(),'supply_slot4_center_cm':stackCenter.tolist(),'supply_panel_rotation':stackR.tolist(),
'panel_center_cm':panelcenter.tolist(),'panel_seating_lift_cm':panel_seating_lift_cm,'sealant_edges':['front','right','left'],'sealant_rear_edge':False}
ww=np.sqrt(1+np.trace(stackR))/2
out['supply_panel_quaternion_xyzw']=[float((stackR[2,1]-stackR[1,2])/(4*ww)),float((stackR[0,2]-stackR[2,0])/(4*ww)),float((stackR[1,0]-stackR[0,1])/(4*ww)),float(ww)]
out['weld_poses']=weld_poses
out['pinch_approach_yaw_deg']=pinch_yaws
out['weld_view_contacts']=[[0,1],[2,3],[4,5]]
out['source_template']='SM_BodyPanel_0386'
out['source_welder_scale']=.7
out['layout_recipe']='left-stations-swapped-v1'
out['source_welder_mount_recipe']='native-cap-direct-v1'
out['source_welder_mount_quaternion_xyzw']=gun_mount_q.tolist()
out['source_welder_mount_translation_cm']=gun_mount_t.tolist()
out['source_welder_tcp_cm']=welder_tip_local.tolist()
out['source_welder_tcp_direction']=welder_direction_local.tolist()
out['welder_rest_pose']=[j['rest_angle_deg'] for j in joints[1]]
out['welder_rest_pose']=[-150.,0.,0.,0.,0.,0.]
out['inspection_mount_quaternion_xyzw']=qmount.tolist()
out['inspection_mount_translation_cm']=[0.,0.,-24.]
out['inspection_rest_pose']=[j['rest_angle_deg'] for j in joints[3]]
out['inspection_rest_pose'][0]=-180.
out['holder_panel_offset_cm']=(-holder_grip_offset).tolist()
out['holder_panel_quaternion_xyzw']=[0.,0.,0.,1.]
# Four small articulated pads contact solid source-panel regions.
# Their struts start on the rack-derived crossbars; no pad floats over a cutout.
out['gripper_pad_mounts']=[]
for pad_index,(px,py) in enumerate([(-53.,-27.),(50.,-36.),(-62.,27.),(44.,27.)]):
    gripper_center=panelcenter+holder_grip_offset
    pp,nn=contact(gripper_center[0]+px,gripper_center[1]+py)
    out['gripper_pad_mounts'].append({'anchor_cm':[44. if pad_index&1 else -44.,27. if pad_index&2 else -27.,-101.],
        'contact_cm':(np.array(pp)+[0,0,panel_seating_lift_cm]-gripper_center).tolist(),'normal':nn,'diameter_cm':8.})
out['pedestal_offsets_cm']=[offsets[0].tolist(),(base_targets[3]-np.array(joints[1][0]['pivot_cm'])).tolist(),(base_targets[1]-np.array(joints[3][0]['pivot_cm'])).tolist()]
out['pinch_gun']={k:v for k,v in pinch.items() if k not in ('parts',)}
out['source_welder_electrode_cm']=source_tcp.tolist()
out['source_welder_direction']=source_pinch.tolist()
out['floor_fixture_offsets_cm']=[[480,0,0],[-480,0,0]]
out['authoring_sha256']=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
output_path=args.output or root/'panel_assembly.json'
output_path.write_text(json.dumps(out,indent=2)+'\n',encoding='utf-8')
print(f'Prepared {len(poses)} role endpoints and {len(cam)} rigid camera endpoints: {output_path}')
