"""Run in Blender 5.0+ with --background --factory-startup --disable-autoexec.

Read the locally supplied CameraExport.blend without saving over it. Bake its
procedural PBR materials and labels into two atlases, evaluate modifiers, and
export two rigid meshes in the existing inspection-camera coordinate frame.
Generated geometry, texture atlases and provenance remain under ignored assets.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import sys

import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def material_signal(material, socket, channel):
    """Translate the source surface tree to an unlit property bake, including mixes."""
    tree = material.node_tree
    if not socket.is_linked:
        return None, 0.
    node = socket.links[0].from_node
    if node.type == 'MIX_SHADER':
        a, av = material_signal(material, node.inputs[1], channel)
        b, bv = material_signal(material, node.inputs[2], channel)
        mix = tree.nodes.new('ShaderNodeMixRGB')
        if node.inputs[0].is_linked:
            tree.links.new(node.inputs[0].links[0].from_socket, mix.inputs[0])
        else:
            mix.inputs[0].default_value = node.inputs[0].default_value
        for index, link, value in [(1, a, av), (2, b, bv)]:
            if link: tree.links.new(link, mix.inputs[index])
            else: mix.inputs[index].default_value = value if hasattr(value, '__len__') else (value, value, value, 1)
        return mix.outputs[0], 0.
    if node.type == 'BSDF_PRINCIPLED':
        name = {'base': 'Base Color', 'rough': 'Roughness', 'metal': 'Metallic'}[channel]
        source = node.inputs[name]
        return (source.links[0].from_socket, 0.) if source.is_linked else (None, source.default_value)
    # Opaque coated-glass proxy: avoid sorting/refraction and preserve sensor isolation.
    if channel == 'base': return None, (.009, .022, .032, 1)
    return None, .12 if channel == 'rough' else .45


def bake(part, output, size):
    scene = bpy.context.scene
    bpy.ops.object.select_all(action='DESELECT')
    part.select_set(True); bpy.context.view_layer.objects.active = part
    # Drop disconnected authoring alternatives; their UV nodes need not survive.
    for m in part.data.materials:
        tree=m.node_tree
        roots=[n for n in tree.nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output]
        used=set()
        def visit(n):
            if n in used: return
            used.add(n)
            for inp in n.inputs:
                for link in inp.links: visit(link.from_node)
        for root in roots: visit(root)
        for n in list(tree.nodes):
            if n not in used: tree.nodes.remove(n)
    # The source already uses all eight Blender UV slots. Preserve their
    # coordinates in named corner attributes while the ninth (bake) UV is active.
    import numpy as np
    source_active=part.data.uv_layers.active.name
    source_uvs=[]
    for layer in part.data.uv_layers:
        values=np.empty(len(layer.data)*2,dtype=np.float32); layer.data.foreach_get('uv',values)
        vectors=np.zeros((len(layer.data),3),dtype=np.float32); vectors[:,:2]=values.reshape(-1,2)
        source_uvs.append((layer.name,vectors))
    for name,vectors in source_uvs:
        attr=part.data.attributes.new('SourceUV_'+name,'FLOAT_VECTOR','CORNER')
        attr.data.foreach_set('vector',vectors.ravel())
    for m in part.data.materials:
        tree=m.node_tree
        def uv_attribute(name):
            n=tree.nodes.new('ShaderNodeAttribute'); n.attribute_name='SourceUV_'+name
            return n.outputs['Vector']
        default_uv=uv_attribute(source_active)
        for n in list(tree.nodes):
            if n.type=='UVMAP':
                vector=uv_attribute(n.uv_map or source_active)
                for link in list(n.outputs[0].links): tree.links.new(vector,link.to_socket)
            if n.type=='TEX_IMAGE' and not n.inputs['Vector'].is_linked:
                tree.links.new(default_uv,n.inputs['Vector'])
            if n.type=='TEX_COORD':
                for link in list(n.outputs['UV'].links): tree.links.new(default_uv,link.to_socket)
    while part.data.uv_layers: part.data.uv_layers.remove(part.data.uv_layers[0])
    uv = part.data.uv_layers.new(name='UnrealAtlas')
    part.data.uv_layers.active = uv; uv.active_render = True
    bpy.ops.object.mode_set(mode='EDIT'); bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=math.radians(66), island_margin=.004, area_weight=.5)
    bpy.ops.object.mode_set(mode='OBJECT')
    materials = list(part.data.materials)
    textures = []
    original = {}
    # Source UV Map nodes remain bound to their named layers. Export only atlas later.
    for m in materials:
        tree=m.node_tree
        out=next(n for n in tree.nodes if n.type=='OUTPUT_MATERIAL' and n.is_active_output)
        original[m.name]=(out, out.inputs['Surface'].links[0].from_socket)
    images={}
    for channel in ['normal', 'base', 'rough', 'metal']:
        img=bpy.data.images.new(part.name+'_'+channel,width=size,height=size,alpha=False)
        img.colorspace_settings.name='sRGB' if channel=='base' else 'Non-Color'
        for m in materials:
            tree=m.node_tree; out,source=original[m.name]
            tree.links.new(source,out.inputs['Surface'])
            tex=tree.nodes.new('ShaderNodeTexImage'); tex.image=img; tree.nodes.active=tex; textures.append((tree,tex))
            if channel!='normal':
                link,value=material_signal(m,out.inputs['Surface'],channel)
                emission=tree.nodes.new('ShaderNodeEmission')
                if link: tree.links.new(link,emission.inputs[0])
                else: emission.inputs[0].default_value=value if hasattr(value,'__len__') else (value,value,value,1)
                tree.links.new(emission.outputs[0],out.inputs['Surface'])
        scene.render.bake.margin=12
        bpy.ops.object.bake(type='NORMAL' if channel=='normal' else 'EMIT')
        images[channel]=img
        if channel in ('base','normal'):
            img.filepath_raw=str(output/(part.name+'_'+channel+'.png')); img.file_format='PNG'; img.save()
    import numpy as np
    rough=np.array(images['rough'].pixels[:],dtype=np.float32).reshape(-1,4)
    metal=np.array(images['metal'].pixels[:],dtype=np.float32).reshape(-1,4)
    orm=bpy.data.images.new(part.name+'_orm',width=size,height=size,alpha=False)
    orm.colorspace_settings.name='Non-Color'
    pixels=np.ones_like(rough); pixels[:,1]=rough[:,0]; pixels[:,2]=metal[:,0]
    orm.pixels.foreach_set(pixels.ravel()); orm.filepath_raw=str(output/(part.name+'_orm.png')); orm.file_format='PNG'; orm.save()
    for m in materials:
        out,source=original[m.name]; m.node_tree.links.new(source,out.inputs['Surface'])
    # Tangents are rebuilt from this sole baked UV channel in Unreal.
    for layer in list(part.data.uv_layers):
        if layer.name!='UnrealAtlas': part.data.uv_layers.remove(layer)
    mat=bpy.data.materials.new(part.name.replace('SM_','M_')); part.data.materials.clear(); part.data.materials.append(mat)
    for face in part.data.polygons: face.material_index=0


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--source',type=Path,default=ROOT/'assets/Camera/CameraExport.blend')
    parser.add_argument('--output',type=Path,default=ROOT/'assets/derived/inspection_camera')
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    if bpy.app.version < (5,0,0): raise RuntimeError('Source requires Blender 5.0 or newer')
    before=sha(args.source); args.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.open_mainfile(filepath=str(args.source),use_scripts=False)
    scene=bpy.context.scene; scene.render.engine='CYCLES'; scene.cycles.samples=8
    scene.cycles.use_denoising=False; scene.render.bake.use_selected_to_active=False
    front=bpy.data.objects['Circle.014']
    axis=(front.matrix_world.to_3x3()@Vector((0,0,1))).normalized()
    up=Vector((0,0,1)); right=up.cross(axis).normalized()
    # Front rim plane; real capture remains 0.5 cm in front of visible geometry.
    rim=front.matrix_world@Vector((0,0,max(p[2] for p in front.bound_box)))
    basis=Matrix((axis,right,up))
    fit=1.5
    sources=[o for o in scene.objects if o.type=='MESH' and not o.hide_render]
    inventory=[]; groups={'Housing':[],'Lens':[]}
    lens_names={'Cylinder','Nut.003','Cube.003','Cube.007','Plane.025'} | {'Circle.%03d'%i for i in range(3,18)}
    for obj in sources:
        inventory.append({'name':obj.name,'vertices':len(obj.data.vertices),'polygons':len(obj.data.polygons)})
        bpy.ops.object.select_all(action='DESELECT'); obj.select_set(True); bpy.context.view_layer.objects.active=obj
        bpy.ops.object.convert(target='MESH')
        # Planar lettering is highly tessellated in the source. Dissolve coplanar
        # edges without changing the letter silhouettes or material boundaries.
        if obj.name.startswith('Text'):
            mod=obj.modifiers.new('Coplanar lettering','DECIMATE'); mod.decimate_type='DISSOLVE'; mod.angle_limit=math.radians(.5)
            bpy.ops.object.modifier_apply(modifier=mod.name)
        for slot in obj.material_slots:
            if not slot.material:
                fallback=bpy.data.materials.get('DFMissingSlot')
                if not fallback:
                    fallback=bpy.data.materials.new('DFMissingSlot'); fallback.use_nodes=True
                    fallback.node_tree.nodes['Principled BSDF'].inputs['Base Color'].default_value=(.035,.035,.035,1)
                slot.material=fallback
        group='Lens' if obj.name in lens_names else 'Housing'; groups[group].append(obj)
    report={'recipe':'imported-camera-rigid-lens-v1','blender':bpy.app.version_string,'source_sha256':before,'source_objects':inventory,
            'uniform_scale':fit,'sensor_lens_local_cm':[21,0,0],'visible_rim_local_cm':[20.5,0,0],
            'source_rim_m':list(rim),'source_forward':list(axis),'meshes':[],'fbx':'inspection_camera.fbx'}
    exported=[]
    for name,objects in groups.items():
        bpy.ops.object.select_all(action='DESELECT')
        for obj in objects: obj.select_set(True)
        bpy.context.view_layer.objects.active=objects[0]; bpy.ops.object.join()
        obj=bpy.context.object; obj.name='SM_InspectionCamera'+name
        bake(obj,args.output,2048 if name=='Housing' else 1024)
        transform=obj.matrix_world.copy()
        # Store metres in Blender; transform normals with the geometry too.
        fit_matrix=(basis*fit).to_4x4()
        fit_matrix.translation=Vector((.205,0,0))-basis@rim*fit
        obj.data.transform(fit_matrix@transform)
        obj.matrix_world=Matrix.Identity(4); obj.data.update()
        obj.data.calc_loop_triangles()
        low=[min(v.co[i] for v in obj.data.vertices)*100 for i in range(3)]
        high=[max(v.co[i] for v in obj.data.vertices)*100 for i in range(3)]
        report['meshes'].append({'id':obj.name,'expected_bounds_min_cm':low,'expected_bounds_max_cm':high,'triangles':len(obj.data.loop_triangles),'material':obj.name.replace('SM_','M_')})
        # Prepared imports intentionally discard FBX node transforms. Put real
        # centimetres in vertices and mirror Blender Y for Unreal's handedness.
        obj.data.transform(Matrix.Diagonal((100.,-100.,100.,1.)))
        obj.data.flip_normals()
        exported.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in exported: obj.select_set(True)
    scene.unit_settings.scale_length=.01
    bpy.ops.export_scene.fbx(filepath=str(args.output/report['fbx']),use_selection=True,object_types={'MESH'},axis_forward='-Y',axis_up='Z',apply_unit_scale=True,apply_scale_options='FBX_SCALE_ALL',bake_space_transform=False,use_mesh_modifiers=True,mesh_smooth_type='FACE',add_leaf_bones=False,bake_anim=False,path_mode='STRIP')
    report['source_unchanged']=before==sha(args.source)
    if not report['source_unchanged']: raise RuntimeError('Source changed during conversion')
    report['generated_sha256']={p.name:sha(p) for p in args.output.iterdir() if p.suffix in ('.fbx','.png')}
    (args.output/'manifest.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print('DF_CAMERA_PREPARED '+json.dumps(report['meshes']))


if __name__=='__main__': main()
