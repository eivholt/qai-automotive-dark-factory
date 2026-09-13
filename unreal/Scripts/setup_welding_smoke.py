"""Rebuild deterministic GPU smoke material only; no source textures required.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file>.
Instance data: [age_seconds, seed01, opacity_envelope]. No shader Time.
Before-DOF translucency is replaced by the existing thermal post-process:
this optical proxy does not model LWIR smoke absorption or gas temperature.
"""
from pathlib import Path
import hashlib
import unreal

PATH = '/Game/Materials/M_WeldingSmoke'
SHADER = Path(__file__).resolve().parents[1] / 'Shaders' / 'WeldingSmoke.usf'
ASSETS = unreal.EditorAssetLibrary
EDITING = unreal.MaterialEditingLibrary


def expression(material, kind, x, y):
    node = EDITING.create_material_expression(material, kind, x, y)
    if node is None:
        raise RuntimeError(f'Cannot create {kind}')
    return node


def connect(source, output, target, pin):
    if not EDITING.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'Cannot connect smoke material pin {pin}')


def generate():
    code = SHADER.read_text(encoding='utf-8')
    if 'FDarkFactorySmoke' not in code or 'OpacityEnvelope' not in code:
        raise RuntimeError('Missing/incomplete canonical smoke shader')
    material = ASSETS.load_asset(PATH) if ASSETS.does_asset_exist(PATH) else None
    if material is None:
        folder, name = PATH.rsplit('/', 1)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f'{PATH} is not a base material')
    EDITING.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)
    material.set_editor_property('translucency_pass', unreal.MaterialTranslucencyPass.MTP_BEFORE_DOF)
    material.set_editor_property('disable_depth_test', False)
    material.set_editor_property('allow_translucent_custom_depth_writes', False)
    material.set_editor_property('screen_space_reflections', False)
    material.set_editor_property('contact_shadows', False)
    material.set_editor_property('enable_responsive_aa', False)

    custom = expression(material, unreal.MaterialExpressionCustom, 100, 0)
    custom.set_editor_property('description', 'Stored-age procedural soft smoke; optical proxy')
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    names = ['LocalPosition', 'NormalWS', 'ViewDirectionWS',
             'AgeSeconds', 'Seed', 'OpacityEnvelope', 'SmokeOpacity']
    inputs = []
    for name in names:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    custom.set_editor_property('inputs', inputs)

    local = expression(material, unreal.MaterialExpressionPreSkinnedPosition, -800, -350)
    local_interp = expression(material, unreal.MaterialExpressionVertexInterpolator, -500, -350)
    connect(local, '', local_interp, 'VS')
    connect(local_interp, '', custom, 'LocalPosition')
    normal = expression(material, unreal.MaterialExpressionPixelNormalWS, -500, -210)
    view = expression(material, unreal.MaterialExpressionCameraVectorWS, -500, -100)
    connect(normal, '', custom, 'NormalWS')
    connect(view, '', custom, 'ViewDirectionWS')
    for index, name in enumerate(['AgeSeconds', 'Seed', 'OpacityEnvelope']):
        data = expression(material, unreal.MaterialExpressionPerInstanceCustomData, -800, 80+index*140)
        data.set_editor_property('data_index', index)
        data.set_editor_property('const_default_value', 0.)
        interp = expression(material, unreal.MaterialExpressionVertexInterpolator, -500, 80+index*140)
        connect(data, '', interp, 'VS')
        connect(interp, '', custom, name)
    opacity = expression(material, unreal.MaterialExpressionScalarParameter, -500, 530)
    opacity.set_editor_property('parameter_name', 'SmokeOpacity')
    opacity.set_editor_property('default_value', .52)
    connect(opacity, '', custom, 'SmokeOpacity')

    fade = expression(material, unreal.MaterialExpressionDepthFade, 370, 0)
    fade.set_editor_property('fade_distance_default', 18.)
    connect(custom, '', fade, 'Opacity')
    if not EDITING.connect_material_property(fade, '', unreal.MaterialProperty.MP_OPACITY):
        raise RuntimeError('Smoke opacity output disconnected')
    color = expression(material, unreal.MaterialExpressionVectorParameter, -500, 730)
    color.set_editor_property('parameter_name', 'SmokeColor')
    color.set_editor_property('default_value', unreal.LinearColor(.22, .22, .22, 1.))
    brightness = expression(material, unreal.MaterialExpressionScalarParameter, -500, 880)
    brightness.set_editor_property('parameter_name', 'SmokeBrightness')
    brightness.set_editor_property('default_value', 1.)
    emission = expression(material, unreal.MaterialExpressionMultiply, 100, 730)
    connect(color, 'RGB', emission, 'A')
    connect(brightness, '', emission, 'B')
    if not EDITING.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError('Smoke color output disconnected')
    EDITING.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    if not material.get_editor_property('used_with_instanced_static_meshes'):
        raise RuntimeError('Smoke instancing material usage missing')
    EDITING.recompile_material(material)
    ASSETS.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_welding_smoke.py/v2')
    ASSETS.set_metadata_tag(material, 'DarkFactoryShaderSHA256', hashlib.sha256(code.encode('utf-8')).hexdigest())
    ASSETS.set_metadata_tag(material, 'DarkFactorySmokeContract',
                            '128 pooled instanced spheres; stored age/seed/opacity; broad noisy soft normal edge; opacity.52; neutral gray.22; DepthFade18cm; beforeDOF; no customdepth; optical-only')
    if not ASSETS.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f'Failed to save {PATH}')
    unreal.log(f'DF_WELDING_SMOKE_MATERIAL_READY path={PATH}')


if __name__ == '__main__':
    generate()
