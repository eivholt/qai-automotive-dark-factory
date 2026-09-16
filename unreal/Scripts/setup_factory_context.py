"""Import optional private Blender/Isaac context and observer-only finishes."""
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
source = root/'assets/derived/factory_context'
assets = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

def material(name, color, roughness, texture=None):
    path = '/Game/FactoryContext/'+name
    mat = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name,'/Game/FactoryContext',unreal.Material,unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(mat)
    mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property('two_sided',True)
    flag = edit.create_material_expression(mat,unreal.MaterialExpressionCustom)
    flag.set_editor_property('code','return (View.PostVolumeUserFlags & 256) != 0 ? 1.0 : 0.0;')
    flag.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    edit.connect_material_property(flag,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    tint = edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
    tint.set_editor_property('constant',unreal.LinearColor(*color))
    edit.connect_material_property(tint,'',unreal.MaterialProperty.MP_BASE_COLOR)
    if texture:
        sample=edit.create_material_expression(mat,unreal.MaterialExpressionTextureSample)
        sample.set_editor_property('texture',texture)
        uv=edit.create_material_expression(mat,unreal.MaterialExpressionTextureCoordinate)
        uv.set_editor_property('u_tiling',18.)
        uv.set_editor_property('v_tiling',18.)
        edit.connect_material_expressions(uv,'',sample,'UVs')
        edit.connect_material_property(sample,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
    emission=edit.create_material_expression(mat,unreal.MaterialExpressionConstant3Vector)
    emission.set_editor_property('constant',unreal.LinearColor(*(v*.45 for v in color)))
    edit.connect_material_property(emission,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    if texture:
        gain=edit.create_material_expression(mat,unreal.MaterialExpressionMultiply)
        gain.set_editor_property('const_b',.22)
        edit.connect_material_expressions(sample,'RGB',gain,'A')
        edit.connect_material_property(gain,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    rough=edit.create_material_expression(mat,unreal.MaterialExpressionConstant)
    rough.set_editor_property('r',roughness)
    edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    edit.set_material_usage(mat,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    edit.recompile_material(mat)
    assets.save_loaded_asset(mat,only_if_is_dirty=False)

if source.is_dir():
    for path in source.glob('*.fbx'):
        task=unreal.AssetImportTask()
        task.filename=str(path); task.destination_path='/Game/FactoryContext'
        task.automated=True; task.replace_existing=True; task.save=True
        options=unreal.FbxImportUI()
        options.import_mesh=True; options.import_materials=False; options.import_textures=False
        options.static_mesh_import_data.set_editor_property('combine_meshes',True)
        options.static_mesh_import_data.set_editor_property('auto_generate_collision',False)
        task.options=options
        tools.import_asset_tasks([task])
    floor_source=root.parent/'qai-physics-reasoning/unreal_conveyor_demo/Content/OmniversePayload/omniverse-content-production.s3-us-west-2.amazonaws.com/Assets/Isaac/6.0/Isaac/Environments/Simple_Warehouse/Materials/Textures/T_Floor_01_D.png'
    task=unreal.AssetImportTask()
    task.filename=str(floor_source);task.destination_path='/Game/FactoryContext';task.automated=True;task.replace_existing=True;task.save=True
    tools.import_asset_tasks([task])
    texture=assets.load_asset('/Game/FactoryContext/T_Floor_01_D')
    texture.set_editor_property('max_texture_size',1024);assets.save_loaded_asset(texture)
    material('M_ContextWall',(.23,.28,.32),.78)
    material('M_ContextRoof',(.13,.17,.21),.65)
    material('M_ContextFloor',(.16,.17,.18),.9,texture)
    material('M_ContextRack',(.04,.16,.31),.55)
    unreal.log('DF_FACTORY_CONTEXT_IMPORTED')
