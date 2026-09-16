"""Blender headless: convert local Isaac warehouse props and author a modular hall.

blender --background --factory-startup --python scripts/assets/prepare_factory_context.py
Source USDs/textures and all generated output remain private.
"""
import hashlib
import json
from pathlib import Path
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT.parent / 'qai-physics-reasoning/unreal_conveyor_demo/Content/OmniversePayload/omniverse-content-production.s3-us-west-2.amazonaws.com/Assets/Isaac/6.0/Isaac/Environments/Simple_Warehouse'
OUT = ROOT / 'assets/derived/factory_context'
OUT.mkdir(parents=True, exist_ok=True)
report = {'blender':bpy.app.version_string,'source':'locally supplied NVIDIA Isaac Simple_Warehouse', 'assets':[]}

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def export(name, source=None):
    meshes = [o for o in bpy.context.scene.objects if o.type == 'MESH']
    bpy.ops.object.select_all(action='DESELECT')
    for obj in meshes: obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = name
    obj.data.materials.clear()
    bpy.context.scene.cursor.location = (0,0,0)
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    path = OUT / (name+'.fbx')
    bpy.ops.export_scene.fbx(filepath=str(path),use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',add_leaf_bones=False)
    row = {'name':name,'triangles':sum(len(p.vertices)-2 for p in obj.data.polygons),'dimensions_m':list(obj.dimensions),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
    if source: row.update(source=str(source),source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())
    report['assets'].append(row)

def box(center, size):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    bpy.context.object.scale = size

def beam(a,b,width=.16):
    a,b = Vector(a),Vector(b)
    box((a+b)/2,(width,width,(b-a).length))
    bpy.context.object.rotation_euler = (b-a).to_track_quat('Z','Y').to_euler()

for source_name,target in [('SM_RackFrame_03','SM_ContextRackFrame'),('SM_RackShelf_01','SM_ContextRackShelf')]:
    reset()
    source = SOURCE/'Props'/(source_name+'.usd')
    bpy.ops.wm.usd_import(filepath=str(source))
    meshes=[o for o in bpy.context.scene.objects if o.type=='MESH']
    points=[o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
    low=Vector(tuple(min(p[i] for p in points) for i in range(3)))
    high=Vector(tuple(max(p[i] for p in points) for i in range(3)))
    offset=Vector(((low.x+high.x)/2,(low.y+high.y)/2,low.z))
    # Bake each source hierarchy before centering the modular asset on its feet.
    for obj in meshes:
        matrix=obj.matrix_world.copy(); obj.parent=None; obj.matrix_world=matrix
        obj.location-=offset
    export(target,source)

reset()
for x in (-24,24):
    for y in range(-36,37,12):
        box((x,y,7),(.4,.4,14))
        box((x,y,.10),(.85,.85,.20))
    box((x,0,5.5),(.16,80,11))
for y in (-40,40):
    # Two loading openings per end wall, clear of the production lane/tunnels.
    for x in (-19,-7,7,19): box((x,y,5.5),(9.8,.16,11))
    box((0,y,12.5),(48,.16,3))
for y in range(-36,37,12):
    beam((-24,y,12.8),(24,y,12.8),.22)
    beam((-24,y,14),(24,y,14),.22)
    for x in range(-24,24,4):
        beam((x,y,12.8),(x+4,y,14),.11)
        beam((x,y,14),(x+4,y,12.8),.11)
export('SM_ContextShell')

reset()
# Roof bands leave long real openings over the cell and skylight frames.
for x in (-19,-9,9,19): box((x,0,14.2),(8,80,.10))
for x in (-14,-4,4,14):
    for y in range(-36,37,12): box((x,y,14.1),(.14,12,.12))
export('SM_ContextRoof')

reset()
box((0,0,-.025),(52,84,.05))
export('SM_ContextFloor')
(OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
print('DF_FACTORY_CONTEXT_READY', OUT, sum(r['triangles'] for r in report['assets']))
