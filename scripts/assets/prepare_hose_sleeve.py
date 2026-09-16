"""Blender background generator: corrugated sleeve, no external asset dependency."""
from pathlib import Path
import json
import math
import bpy

root = Path(__file__).resolve().parents[2]
out = root / 'assets/derived/hose_sleeve'
out.mkdir(parents=True, exist_ok=True)
bpy.ops.object.select_all(action='SELECT')
bpy.ops.object.delete(use_global=False)
# A unit cylinder, matching Unreal's 100 cm BasicShapes cylinder bounds.
# 48 axial folds, 16 radial sides: 6,176 triangles with caps.
verts=[]; faces=[]
segments=192; sides=16
for ring in range(segments+1):
    z=ring/segments-.5
    radius=.46+.04*(.5+.5*math.cos(ring*math.pi/2))
    for side in range(sides):
        angle=side*2*math.pi/sides
        verts.append((radius*math.cos(angle),radius*math.sin(angle),z))
for ring in range(segments):
    for side in range(sides):
        a=ring*sides+side; b=ring*sides+(side+1)%sides
        faces.append((a,b,b+sides,a+sides))
faces += [tuple(reversed(range(sides))),tuple(segments*sides+i for i in range(sides))]
mesh=bpy.data.meshes.new('CorrugatedSleeve');mesh.from_pydata(verts,[],faces);mesh.update()
obj=bpy.data.objects.new('SM_HoseSleeve',mesh);bpy.context.collection.objects.link(obj)
bpy.context.view_layer.objects.active=obj;obj.select_set(True)
uv=mesh.uv_layers.new(name='UVMap')
for poly in mesh.polygons:
    poly.use_smooth=len(poly.vertices)==4
    for index in poly.loop_indices:
        v=mesh.loops[index].vertex_index; uv.data[index].uv=((v%sides)/sides,(v//sides)/segments)
bpy.ops.export_scene.fbx(filepath=str(out/'SM_HoseSleeve.fbx'),use_selection=True,apply_unit_scale=True,
    axis_forward='-Y',axis_up='Z',add_leaf_bones=False,bake_anim=False)
(out/'manifest.json').write_text(json.dumps({'generator':'prepare_hose_sleeve.py/v1','axial_folds':48,'radial_sides':sides,
    'vertices':len(verts),'triangles':segments*sides*2+2*(sides-2),'source':'procedural'},indent=2))
