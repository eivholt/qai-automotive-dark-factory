"""Generate the private GPU LCD/button material on the existing cabinet face mesh.

Run after local welding-cell import, via UnrealEditor-Cmd -run=pythonscript.
Only /Game/Materials/M_ControlCabinet is created/rebuilt. The imported material,
texture and meshes are loaded read-only. Parameters are simulation graphics,
not live machine telemetry. No image, font, render target or Time node is added.
"""
from pathlib import Path
import hashlib
import json
import unreal

ROOT = Path(__file__).resolve().parents[2]
PATH = '/Game/Materials/M_ControlCabinet'
SOURCE_TEXTURE = '/Game/WeldingCell/Textures/T_WC_robots_screen'
SHADER = ROOT / 'unreal/Shaders/ControlCabinet.usf'
ASSETS = unreal.EditorAssetLibrary
EDIT = unreal.MaterialEditingLibrary
VECTORS = {
    'ScreenCenter': (0., 0., 0.),
    'ScreenRight': (1., 0., 0.),
    'ScreenUp': (0., 0., 1.),
    'ScreenNormal': (0., -1., 0.),
    **{f'ButtonCenter{i}': (0., 0., 0.) for i in range(12)},
}
SCALARS = {
    'ScreenWidth': 43.4247, 'ScreenHeight': 32.754,
    'ScreenStrength': 2.5, 'ButtonStrength': 5., 'PanelPower': 1.,
    'CycleProgress': 0., 'CycleSeconds': 0., 'ActiveRobotCount': 0., 'Paused': 0.,
    'ProcessMode': 0., 'ProcessBody': 0., 'ProcessStageSeconds': 0.,
    'ProcessPassCount': 0., 'ProcessActiveGate': -1.,
    **{f'ProcessGate{i}': 0. for i in range(4)},
    **{f'RobotPhase{i}': 0. for i in range(4)},
    **{f'RobotProgress{i}': 0. for i in range(4)},
    **{f'ButtonRadius{i}': 0. for i in range(12)},
    **{f'ButtonLevel{i}': 0. for i in range(12)},
}
MASK_INPUTS = ['WorldPosition', 'ScreenCenter', 'ScreenRight', 'ScreenUp',
               'ScreenNormal', 'ScreenWidth', 'ScreenHeight']
MASK_CODE = r"""
float3 D=WorldPosition-ScreenCenter;
float2 UV=float2(dot(D,normalize(ScreenRight))/max(ScreenWidth,1.)+.5,
                 .5-dot(D,normalize(ScreenUp))/max(ScreenHeight,1.));
float AA=max(.00045,max(fwidth(UV.x),fwidth(UV.y))*.60);
float2 Edge=max(float2(.035,.045)-UV,UV-float2(.965,.955));
return (1.-smoothstep(-AA,AA,max(Edge.x,Edge.y)))
       *(1.-smoothstep(.60,1.,abs(dot(D,normalize(ScreenNormal)))));
"""


def expression(material, kind, x, y, name):
    node = EDIT.create_material_expression(material, kind, x, y)
    if node is None:
        raise RuntimeError(f'Cannot create control-cabinet expression {name}')
    node.set_editor_property('desc', 'DF_CONTROL_CABINET_V1/' + name)
    return node


def connect(source, output, target, pin):
    if not EDIT.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'Cannot connect control-cabinet pin {pin}')


def property_output(node, output, prop):
    if not EDIT.connect_material_property(node, output, prop):
        raise RuntimeError(f'Cannot connect control-cabinet material output {prop}')


def custom(material, name, code, names, output_type, x, y):
    node = expression(material, unreal.MaterialExpressionCustom, x, y, name)
    node.set_editor_property('description', 'Control cabinet: ' + name)
    node.set_editor_property('code', code)
    node.set_editor_property('output_type', output_type)
    entries = []
    for name in names:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        entries.append(entry)
    node.set_editor_property('inputs', entries)
    return node


def generate():
    code = SHADER.read_text(encoding='utf-8')
    if 'struct FControlCabinet' not in code or 'RobotProgress3' not in code:
        raise RuntimeError('Missing/incomplete canonical control-cabinet shader')
    texture = ASSETS.load_asset(SOURCE_TEXTURE)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError('Import the private welding-cell screen texture first: ' + SOURCE_TEXTURE)
    material = ASSETS.load_asset(PATH) if ASSETS.does_asset_exist(PATH) else None
    if material is None:
        folder, name = PATH.rsplit('/', 1)
        material = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f'{PATH} is not a base material')
    # UE 5.8's bulk helper mutates the collection while iterating it.
    # Snapshot first so replay removes every old expression exactly once.
    for old_expression in list(EDIT.get_material_expressions(material)):
        EDIT.delete_material_expression(material, old_expression)
    if EDIT.get_num_material_expressions(material) != 0:
        raise RuntimeError('Cabinet material replay left stale graph expressions')
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_DEFAULT_LIT)
    material.set_editor_property('two_sided', False)

    texture_node = expression(material, unreal.MaterialExpressionTextureSample, -1350, -300, 'source_atlas')
    texture_node.set_editor_property('texture', texture)
    texture_node.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    world = expression(material, unreal.MaterialExpressionWorldPosition, -1350, -150, 'world_position')
    parameters = {}
    for index, (name, value) in enumerate(VECTORS.items()):
        node = expression(material, unreal.MaterialExpressionVectorParameter, -1350, index * 140 + 100, name)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', unreal.LinearColor(*value, 0.))
        parameters[name] = (node, 'RGB')
    for index, (name, value) in enumerate(SCALARS.items()):
        node = expression(material, unreal.MaterialExpressionScalarParameter, -1050, index * 140 + 100, name)
        node.set_editor_property('parameter_name', name)
        node.set_editor_property('default_value', value)
        parameters[name] = (node, '')
    all_inputs = ['WorldPosition', 'SourceColor'] + list(VECTORS) + list(SCALARS)
    emission = custom(material, 'GPU LCD gauges and source-colored LED lenses', code,
                      all_inputs, unreal.CustomMaterialOutputType.CMOT_FLOAT3, 0, 200)
    mask = custom(material, 'LCD glass base mask', MASK_CODE, MASK_INPUTS,
                  unreal.CustomMaterialOutputType.CMOT_FLOAT1, 0, -400)
    sources = {'WorldPosition': (world, ''), 'SourceColor': (texture_node, 'RGB'), **parameters}
    for name in all_inputs:
        connect(*sources[name], emission, name)
    for name in MASK_INPUTS:
        connect(*sources[name], mask, name)
    property_output(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    glass = expression(material, unreal.MaterialExpressionConstant3Vector, 0, -600, 'dark_lcd_glass')
    glass.set_editor_property('constant', unreal.LinearColor(.004, .009, .014, 1.))
    base = expression(material, unreal.MaterialExpressionLinearInterpolate, 500, -300, 'source_bezel_and_lcd_glass')
    connect(texture_node, 'RGB', base, 'A')
    connect(glass, '', base, 'B')
    connect(mask, '', base, 'Alpha')
    property_output(base, '', unreal.MaterialProperty.MP_BASE_COLOR)
    # The active LCD cover is matte, avoiding a frontal NIR reflection that
    # overwhelms its small emissive gauges. Source bezel/button finish is kept.
    for name, source_value, lcd_value, prop, y in [
            ('roughness', .32, .75, unreal.MaterialProperty.MP_ROUGHNESS, 50),
            ('specular', .35, 0., unreal.MaterialProperty.MP_SPECULAR, 250)]:
        node = expression(material, unreal.MaterialExpressionLinearInterpolate, 500, y, name)
        node.set_editor_property('const_a', source_value)
        node.set_editor_property('const_b', lcd_value)
        connect(mask, '', node, 'Alpha')
        property_output(node, '', prop)
    metallic = expression(material, unreal.MaterialExpressionConstant, 500, 150, 'metallic')
    metallic.set_editor_property('r', 0.)
    property_output(metallic, '', unreal.MaterialProperty.MP_METALLIC)
    EDIT.recompile_material(material)
    digest = hashlib.sha256(code.encode('utf-8')).hexdigest()
    ASSETS.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_control_cabinet.py/v1')
    ASSETS.set_metadata_tag(material, 'DarkFactoryShaderSHA256', digest)
    ASSETS.set_metadata_tag(material, 'DarkFactoryControlCabinetContract',
                            'existing 13-island opaque mesh; original source atlas; world-frame LCD and 12 LED masks; simulation-clock scalar parameters; no Time/render-target/new thermal surface')
    if not ASSETS.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f'Failed to save {PATH}')
    report = {
        'material': PATH, 'source_texture': SOURCE_TEXTURE, 'shader_sha256': digest,
        'scalar_defaults': SCALARS, 'vector_defaults': VECTORS,
        'lcd_shader_output': 'float3 emission', 'material_domain': 'opaque default lit',
        'new_texture_assets': 0, 'render_targets': 0, 'time_nodes': 0,
        'imported_source_modified': False,
        'source_finish': {'roughness': .32, 'specular': .35},
        'lcd_finish': {'roughness': .75, 'specular': 0.},
    }
    (ROOT / 'artifacts').mkdir(exist_ok=True)
    (ROOT / 'artifacts/control-cabinet-material.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    unreal.log(f'DF_CONTROL_CABINET_MATERIAL_READY path={PATH} scalar_parameters={len(SCALARS)} vector_parameters={len(VECTORS)}')
    return report


if __name__ == '__main__':
    generate()
