"""Rebuild the fixed imported-scene NIR grayscale material only.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
The imported fixed sensor binds this material with persistent rendering state;
procedural training capture and observer display materials remain independent.
"""
from pathlib import Path
import hashlib
import unreal

PATH = '/Game/Materials/M_WeldingNIRGrayscale'
SHADER = Path(__file__).resolve().parents[1] / 'Shaders' / 'WeldingNIRGrayscale.usf'
ASSETS = unreal.EditorAssetLibrary
EDITING = unreal.MaterialEditingLibrary


def generate():
    code = SHADER.read_text(encoding='utf-8')
    if 'float Gray = dot(SceneColor.rgb' not in code or 'return Gray.xxx;' not in code:
        raise RuntimeError('Missing/incomplete canonical fixed NIR grayscale shader')
    material = ASSETS.load_asset(PATH) if ASSETS.does_asset_exist(PATH) else None
    if material is None:
        folder, name = PATH.rsplit('/', 1)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f'{PATH} is not a base material')
    EDITING.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
    material.set_editor_property('blendable_priority', 100)

    scene = EDITING.create_material_expression(material, unreal.MaterialExpressionSceneTexture, -350, 0)
    custom = EDITING.create_material_expression(material, unreal.MaterialExpressionCustom, 0, 0)
    if scene is None or custom is None:
        raise RuntimeError('Cannot create fixed NIR material expressions')
    scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    scene.set_editor_property('filtered', False)
    custom.set_editor_property('description', 'Fixed imported NIR: final Rec.709 grayscale only')
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    source = unreal.CustomInput()
    source.set_editor_property('input_name', 'SceneColor')
    custom.set_editor_property('inputs', [source])
    if not EDITING.connect_material_expressions(scene, 'Color', custom, 'SceneColor'):
        raise RuntimeError('Fixed NIR PostProcessInput0 disconnected')
    if not EDITING.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Fixed NIR grayscale output disconnected')
    EDITING.recompile_material(material)
    ASSETS.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_welding_nir.py/v1')
    ASSETS.set_metadata_tag(material, 'DarkFactoryShaderSHA256', hashlib.sha256(code.encode('utf-8')).hexdigest())
    ASSETS.set_metadata_tag(material, 'DarkFactoryNIRContract',
                            'imported fixed camera only; after-tonemap; one unfiltered sample; Rec709 replicated RGB; no gain/noise/gamma/time; persistent view state required')
    if not ASSETS.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f'Failed to save {PATH}')
    unreal.log(f'DF_WELDING_NIR_MATERIAL_READY path={PATH}')


if __name__ == '__main__':
    generate()
