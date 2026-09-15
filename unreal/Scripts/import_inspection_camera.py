"""Import the private Blender camera prepared by prepare_inspection_camera.py.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file> -NullRHI.
Reuses the audited FBX path without changing welding-cell source packages.
"""
from pathlib import Path
import json
import runpy
import unreal

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'assets/derived/inspection_camera'
DEST='/Game/InspectionCamera'
API=runpy.run_path(str(Path(__file__).with_name('import_welding_cell.py')))
ASSETS=unreal.EditorAssetLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
EDIT=unreal.MaterialEditingLibrary


def texture(path, kind):
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(path),destination_path=DEST+'/Textures',destination_name='T_'+path.stem,automated=True,replace_existing=True,save=True).items():
        task.set_editor_property(key,value)
    TOOLS.import_asset_tasks([task])
    asset=ASSETS.load_asset(DEST+'/Textures/T_'+path.stem)
    if not isinstance(asset,unreal.Texture2D): raise RuntimeError('Missing texture '+str(path))
    asset.set_editor_property('srgb',kind=='base')
    if kind=='normal':
        asset.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP)
        asset.set_editor_property('flip_green_channel',True)
    elif kind=='orm': asset.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
    ASSETS.save_loaded_asset(asset)
    return asset


def material(name, mesh_name):
    path=DEST+'/Materials/'+name
    asset=ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(name,DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
    API['clear_material_graph'](asset)
    for kind,y in [('base',0),('normal',200),('orm',400)]:
        tex=texture(SOURCE/(mesh_name+'_'+kind+'.png'),kind)
        node=EDIT.create_material_expression(asset,unreal.MaterialExpressionTextureSample,-350,y)
        node.set_editor_property('texture',tex)
        sampler={'base':unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,'normal':unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,'orm':unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[kind]
        node.set_editor_property('sampler_type',sampler)
        if kind=='base': EDIT.connect_material_property(node,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
        elif kind=='normal': EDIT.connect_material_property(node,'RGB',unreal.MaterialProperty.MP_NORMAL)
        else:
            EDIT.connect_material_property(node,'G',unreal.MaterialProperty.MP_ROUGHNESS)
            EDIT.connect_material_property(node,'B',unreal.MaterialProperty.MP_METALLIC)
    EDIT.recompile_material(asset); ASSETS.save_loaded_asset(asset)
    return asset


def main():
    manifest=json.loads((SOURCE/'manifest.json').read_text())
    original=ROOT/'assets/Camera/CameraExport.blend'
    if API['sha256'](original)!=manifest['source_sha256']: raise RuntimeError('Camera source changed; regenerate first')
    for name,digest in manifest['generated_sha256'].items():
        if API['sha256'](SOURCE/name)!=digest: raise RuntimeError('Camera generated file changed: '+name)
    API['import_meshes'](SOURCE/manifest['fbx'],DEST+'/Meshes',True)
    for item in manifest['meshes']:
        b=ASSETS.load_asset(DEST+'/Meshes/'+item['id']).get_bounds()
        unreal.log('CAMERA_BOUNDS '+item['id']+' '+str(b))
    audit=API['validate_prepared'](manifest,DEST+'/Meshes')
    rows=[]
    subsystem=unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem) or unreal.new_object(unreal.StaticMeshEditorSubsystem)
    for item in manifest['meshes']:
        mesh=ASSETS.load_asset(DEST+'/Meshes/'+item['id'])
        mesh.set_material(0,material(item['material'],item['id']))
        # Two draw calls and progressively cheaper GPU meshes at distance.
        options=unreal.EditorScriptingMeshReductionOptions()
        reductions=[]
        for percent,screen in [(.4,1.),(.15,.25),(.04,.10)]:
            reduction=unreal.EditorScriptingMeshReductionSettings()
            reduction.set_editor_property('percent_triangles',percent)
            reduction.set_editor_property('screen_size',screen)
            reductions.append(reduction)
        options.set_editor_property('reduction_settings',reductions)
        options.set_editor_property('auto_compute_lod_screen_size',False)
        subsystem.set_lods(mesh,options)
        if mesh.get_num_lods()!=3 or any(mesh.get_num_triangles(i)<=0 for i in range(3)):
            raise RuntimeError('Camera LOD generation failed: '+item['id'])
        # Collider overlay and visibility queries use fitted simple boxes.
        subsystem.remove_collisions(mesh)
        if subsystem.add_simple_collisions(mesh,unreal.ScriptingCollisionShapeType.BOX)<0:
            raise RuntimeError('Camera collision generation failed: '+item['id'])
        ASSETS.save_loaded_asset(mesh)
        rows.append({'mesh':mesh.get_path_name(),'lod_triangles':[mesh.get_num_triangles(i) for i in range(mesh.get_num_lods())],'material':mesh.get_material(0).get_path_name()})
    report={'passed':True,'recipe':manifest['recipe'],'source_sha256':manifest['source_sha256'],'bounds':audit,'meshes':rows}
    dest=ROOT/'artifacts/asset-inspection/inspection_camera_import.json'; dest.parent.mkdir(parents=True,exist_ok=True)
    dest.write_text(json.dumps(report,indent=2)+'\n')
    unreal.log('DF_INSPECTION_CAMERA_IMPORTED '+json.dumps(rows))


if __name__=='__main__': main()
