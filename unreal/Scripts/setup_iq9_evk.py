"""Generate only the locally sourced IQ9 EVK prop materials and appearance plates.

First run scripts/assets/prepare_iq9_evk.py. Generated textures/packages remain
private; this script does not access or modify the previous Unreal project.
No levels or actor placements are edited here.
"""
from pathlib import Path
import hashlib
import json
import unreal

FOLDER = '/Game/IQ9EVK'
ASSETS = unreal.EditorAssetLibrary
EDITING = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def node(material, kind, x, y):
    result = EDITING.create_material_expression(material, kind, x, y)
    if result is None:
        raise RuntimeError(f'Cannot create IQ9 {kind}')
    return result


def connect(source, output, target, pin):
    if not EDITING.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'Cannot connect IQ9 material {pin}')


def output(expression, pin, prop):
    if not EDITING.connect_material_property(expression, pin, prop):
        raise RuntimeError(f'Cannot connect IQ9 material output {prop}')


def material(name):
    path = FOLDER+'/'+name
    value = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if value is None:
        value = TOOLS.create_asset(name,FOLDER,unreal.Material,unreal.MaterialFactoryNew())
    if not isinstance(value,unreal.Material):
        raise RuntimeError(f'{path} is not a Material')
    EDITING.delete_all_material_expressions(value)
    value.set_editor_property('material_domain',unreal.MaterialDomain.MD_SURFACE)
    value.set_editor_property('blend_mode',unreal.BlendMode.BLEND_OPAQUE)
    value.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    return value


def constant(mat, value, prop, y):
    expression = node(mat,unreal.MaterialExpressionConstant,-300,y)
    expression.set_editor_property('r',value)
    output(expression,'',prop)


def save(mat):
    EDITING.recompile_material(mat)
    ASSETS.set_metadata_tag(mat,'DarkFactoryGenerator','setup_iq9_evk.py/v1')
    if not ASSETS.save_loaded_asset(mat,only_if_is_dirty=False):
        raise RuntimeError(f'Cannot save {mat.get_path_name()}')


def generate():
    root = Path(unreal.Paths.project_dir()).resolve().parent/'assets/iq9_evk'
    manifest = json.loads((root/'mounts.json').read_text(encoding='utf-8'))
    for entry in manifest['files']:
        path = (root/entry['file']).resolve()
        if not path.is_relative_to(root.resolve()) or hashlib.sha256(path.read_bytes()).hexdigest()!=entry['sha256']:
            raise RuntimeError('IQ9 texture path/hash invalid; rerun prepare_iq9_evk.py')
        face = entry['face'].title()
        name = 'T_IQ9_'+face
        task = unreal.AssetImportTask()
        for key,value in dict(filename=str(path),destination_path=FOLDER,destination_name=name,
                              replace_existing=True,automated=True,save=True).items():
            task.set_editor_property(key,value)
        TOOLS.import_asset_tasks([task])
        texture = ASSETS.load_asset(FOLDER+'/'+name)
        if not isinstance(texture,unreal.Texture2D):
            raise RuntimeError(f'IQ9 texture import failed: {face}')
        texture.set_editor_property('srgb',True)
        texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_DEFAULT)
        texture.set_editor_property('max_texture_size',1024)
        texture.set_editor_property('lod_group',unreal.TextureGroup.TEXTUREGROUP_WORLD)
        ASSETS.set_metadata_tag(texture,'DarkFactorySourceSHA256',entry['sha256'])
        if not ASSETS.save_loaded_asset(texture,only_if_is_dirty=False):
            raise RuntimeError('Cannot save IQ9 texture')
        mat = material('M_IQ9_'+face)
        mat.set_editor_property('two_sided',True)
        image = node(mat,unreal.MaterialExpressionTextureSampleParameter2D,-500,-150)
        image.set_editor_property('parameter_name','FaceTexture')
        image.set_editor_property('texture',texture)
        image.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        output(image,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
        constant(mat,.60 if face=='Top' else .48,unreal.MaterialProperty.MP_ROUGHNESS,100)
        constant(mat,.38,unreal.MaterialProperty.MP_SPECULAR,230)
        ASSETS.set_metadata_tag(mat,'DarkFactorySourceSHA256',entry['sha256'])
        save(mat)
    for name,rgb,roughness,metallic in (
            ('M_IQ9_Body',(.023,.023,.823),.52,0.),
            ('M_IQ9_Plate',(.28,.31,.34),.48,.40)):
        mat = material(name)
        color = node(mat,unreal.MaterialExpressionConstant3Vector,-350,-100)
        color.set_editor_property('constant',unreal.LinearColor(*rgb,1.))
        output(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
        constant(mat,roughness,unreal.MaterialProperty.MP_ROUGHNESS,100)
        constant(mat,metallic,unreal.MaterialProperty.MP_METALLIC,230)
        save(mat)
    mat = material('M_IQ9_LED')
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_UNLIT)
    color = node(mat,unreal.MaterialExpressionVectorParameter,-500,-100)
    color.set_editor_property('parameter_name','LedColor')
    color.set_editor_property('default_value',unreal.LinearColor(.01,1.,.05,1.))
    strength = node(mat,unreal.MaterialExpressionScalarParameter,-500,80)
    strength.set_editor_property('parameter_name','LedStrength')
    strength.set_editor_property('default_value',0.)
    multiply = node(mat,unreal.MaterialExpressionMultiply,-120,-40)
    connect(color,'RGB',multiply,'A'); connect(strength,'',multiply,'B')
    output(multiply,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    save(mat)
    unreal.log('DF_IQ9_CONTENT_READY textures=5 materials=8 source_hashes_verified=true')


if __name__=='__main__':
    generate()
