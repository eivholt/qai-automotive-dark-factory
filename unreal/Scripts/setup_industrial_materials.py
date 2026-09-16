"""Build optional observer-only Isaac rubber/metal finishes (three texture samples)."""
from pathlib import Path
import unreal

root = Path(__file__).resolve().parents[2]
source = root / 'assets/derived/industrial_materials'
assets = unreal.EditorAssetLibrary
edit = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()

def build(kind, texture_name):
    textures = {}
    for suffix in ('Albedo', 'Normal', 'ORM'):
        name = f'T_{texture_name}_{suffix}'
        path = '/Game/IndustrialFinishes/' + name
        task = unreal.AssetImportTask()
        task.filename = str(source / (name + '.png'))
        task.destination_path = '/Game/IndustrialFinishes'
        task.automated = True
        task.replace_existing = True
        task.save = False
        tools.import_asset_tasks([task])
        texture = assets.load_asset(path)
        texture.set_editor_property('max_texture_size', 1024)
        texture.set_editor_property('srgb', suffix == 'Albedo')
        if suffix == 'Normal':
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_NORMALMAP)
        elif suffix == 'ORM':
            texture.set_editor_property('compression_settings', unreal.TextureCompressionSettings.TC_MASKS)
        assets.save_loaded_asset(texture)
        textures[suffix] = texture
    name = 'M_Industrial' + kind
    path = '/Game/Materials/' + name
    material = assets.load_asset(path) if assets.does_asset_exist(path) else tools.create_asset(name, '/Game/Materials', unreal.Material, unreal.MaterialFactoryNew())
    edit.delete_all_material_expressions(material)
    custom = edit.create_material_expression(material, unreal.MaterialExpressionCustom, 0, 0)
    custom.set_editor_property('code', (root / 'unreal/Shaders/IndustrialSurface.usf').read_text())
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    inputs = []
    for name in ('BaseColor','Roughness','Metallic','UV','Albedo','SurfaceNormal','ORM'):
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    custom.set_editor_property('inputs', inputs)
    outputs = []
    for name, kind_out in [('NormalOut',unreal.CustomMaterialOutputType.CMOT_FLOAT3),('RoughnessOut',unreal.CustomMaterialOutputType.CMOT_FLOAT1)]:
        entry = unreal.CustomOutput()
        entry.set_editor_property('output_name', name)
        entry.set_editor_property('output_type', kind_out)
        outputs.append(entry)
    custom.set_editor_property('additional_outputs', outputs)
    color = edit.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -600, 0)
    color.set_editor_property('parameter_name', 'BaseColor')
    color.set_editor_property('default_value', unreal.LinearColor(.025,.026,.028,1))
    edit.connect_material_expressions(color, '', custom, 'BaseColor')
    for param, value in [('Roughness',.95),('Metallic',0.)]:
        node = edit.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -600, 150)
        node.set_editor_property('parameter_name', param)
        node.set_editor_property('default_value', value)
        if param == 'Roughness': edit.connect_material_expressions(node,'',custom,param)
        else:
            edit.connect_material_property(node,'',unreal.MaterialProperty.MP_METALLIC)
            edit.connect_material_expressions(node,'',custom,'Metallic')
    uv = edit.create_material_expression(material, unreal.MaterialExpressionTextureCoordinate, -1000, 400)
    uv.set_editor_property('u_tiling', 3.)
    uv.set_editor_property('v_tiling', 3.)
    edit.connect_material_expressions(uv,'',custom,'UV')
    for suffix, input_name in [('Albedo','Albedo'),('Normal','SurfaceNormal'),('ORM','ORM')]:
        sample = edit.create_material_expression(material, unreal.MaterialExpressionTextureSample, -600, 400)
        sample.set_editor_property('texture', textures[suffix])
        sample.set_editor_property('sampler_type', {'Albedo':unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,'Normal':unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,'ORM':unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}[suffix])
        edit.connect_material_expressions(uv,'',sample,'UVs')
        edit.connect_material_expressions(sample,'RGB',custom,input_name)
    for output, prop in [('',unreal.MaterialProperty.MP_BASE_COLOR),('NormalOut',unreal.MaterialProperty.MP_NORMAL),('RoughnessOut',unreal.MaterialProperty.MP_ROUGHNESS)]:
        edit.connect_material_property(custom, output, prop)
    edit.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_SPLINE_MESH)
    edit.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    edit.recompile_material(material)
    assets.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_industrial_materials.py/observer-bit8-v1')
    assets.save_loaded_asset(material, only_if_is_dirty=False)

if source.is_dir():
    build('Rubber','Rubber_Black_Rough_A')
    build('Metal','Metal_Rough_A')
    unreal.log('DF_INDUSTRIAL_FINISH_READY')
else:
    unreal.log('DF_INDUSTRIAL_FINISH_SKIPPED: optional local textures absent')
