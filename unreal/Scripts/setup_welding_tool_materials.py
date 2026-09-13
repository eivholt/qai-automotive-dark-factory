"""Add authored tool and car-contact heat glow to existing local PBR materials.

Run with UnrealEditor-Cmd DarkFactory.uproject -run=pythonscript
-script=<absolute path to this file> -unattended -NullRHI -nosplash.
Optional -DFToolMaterialReport=<absolute JSON path>. Stop the interactive editor
before this commandlet writes its loaded assets. No source meshes are imported.

The importer calls augment_materials after rebuilding its ordinary PBR graphs.
Only existing /Game/WeldingCell/Materials/M_WC_* assets are eligible. Shared
ToolHeatGlow and WeldHeatGlow0..3 default to zero; runtime uses separate MIDs.
V2 retains the complete audited V1 tool graph and prior source emission, adding
one four-contact custom expression through a new final emissive addition.
The fixed NIR capture's existing post-process desaturation preserves grayscale;
verify rendered neutrality after changes, including hot and cooling states.
"""
from pathlib import Path
import hashlib
import json
import re
import time
import traceback
import unreal

ROOT = Path(__file__).resolve().parents[2]
FOLDER = '/Game/WeldingCell/Materials'
VERSION_V1 = 'setup_welding_tool_materials.py/v1'
VERSION = 'setup_welding_tool_materials.py/v2'
TAG = 'DarkFactoryToolHeatGenerator'
# V1 node ownership remains stable: upgrades do not replace the tool branch.
PREFIX = 'DF_TOOL_HEAT_V1/'
WELD_PREFIX = 'DF_WELD_HEAT_V2/'
EDIT = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary
SCALARS = {'ToolHeatGlow': 0., 'ToolHeatRadiusCm': 26.}
VECTORS = {'ToolHotspotWorld': (0., 0., 0., 0.),
           'ToolGlowColor': (1., .24, .02, 1.)}
CODE = '''// Authored emission proxy; matches the thermal hotspot's spatial kernel.
float3 D = WorldPosition.xyz - ToolHotspotWorld.xyz;
float Radius = max(abs(ToolHeatRadiusCm), 0.01);
float S = saturate(1.0 - dot(D, D) / (Radius * Radius));
float Weight = S * S * (3.0 - 2.0 * S);
return max(ToolHeatGlow, 0.0) * max(ToolGlowColor.rgb, 0.0) * Weight;
'''
WELD_PATCH_COUNT = 4
WELD_SCALARS = {name: value for index in range(WELD_PATCH_COUNT)
                for name, value in ((f'WeldHeatRadiusCm{index}', 8.), (f'WeldHeatGlow{index}', 0.))}
WELD_VECTORS = {name: value for index in range(WELD_PATCH_COUNT)
                for name, value in ((f'WeldHotspotWorld{index}', (0., 0., 0., 0.)),
                                    (f'WeldGlowColor{index}', (1., .24, .02, 1.)))}
WELD_CODE = '// Authored surface-contact emission; the same compact kernel as tool heat.\nfloat3 Emission = 0.0;\n'
for _index in range(WELD_PATCH_COUNT):
    WELD_CODE += f'''float3 D{_index} = WorldPosition.xyz - WeldHotspotWorld{_index}.xyz;
float Radius{_index} = max(abs(WeldHeatRadiusCm{_index}), 0.01);
float S{_index} = saturate(1.0 - dot(D{_index}, D{_index}) / (Radius{_index} * Radius{_index}));
float Weight{_index} = S{_index} * S{_index} * (3.0 - 2.0 * S{_index});
Emission += max(WeldHeatGlow{_index}, 0.0) * max(WeldGlowColor{_index}.rgb, 0.0) * Weight{_index};
'''
WELD_CODE += 'return Emission;\n'
CODE_SHA256 = hashlib.sha256(CODE.encode('utf-8')).hexdigest()
WELD_CODE_SHA256 = hashlib.sha256(WELD_CODE.encode('utf-8')).hexdigest()
SHADER_SHA256 = hashlib.sha256((CODE + WELD_CODE).encode('utf-8')).hexdigest()
PRESERVED_PROPERTIES = {
    'base_color': unreal.MaterialProperty.MP_BASE_COLOR,
    'roughness': unreal.MaterialProperty.MP_ROUGHNESS,
    'metallic': unreal.MaterialProperty.MP_METALLIC,
}
EMISSIVE = unreal.MaterialProperty.MP_EMISSIVE_COLOR


def graph_inputs(material):
    result = {}
    for name, prop in PRESERVED_PROPERTIES.items():
        value = EDIT.get_material_property_input_node(material, prop)
        result[name] = {'node': value.get_path_name() if value else None,
                        'output': str(EDIT.get_material_property_input_node_output_name(material, prop))}
    return result


def node(material, kind, role, x, y, prefix=PREFIX):
    value = EDIT.create_material_expression(material, kind, x, y)
    if value is None:
        raise RuntimeError('Cannot create heat expression: ' + role)
    value.set_editor_property('desc', prefix + role)
    return value


def connect(source, output, target, input_name):
    if not EDIT.connect_material_expressions(source, output, target, input_name):
        raise RuntimeError('Cannot connect heat input: ' + input_name)


def owned_nodes(material, prefix=PREFIX):
    result = {}
    for expression in EDIT.get_material_expressions(material):
        description = str(expression.get_editor_property('desc'))
        if description.startswith(prefix):
            role = description[len(prefix):]
            if role in result:
                raise RuntimeError('Duplicate generated heat node: ' + prefix + role)
            result[role] = expression
    return result


def audit_custom(material, custom, nodes, scalars, vectors, code, world):
    if not isinstance(custom, unreal.MaterialExpressionCustom) or custom.get_editor_property('code') != code:
        raise RuntimeError('Generated heat HLSL differs from current generator')
    for name, value in scalars.items():
        parameter = nodes[name]
        if str(parameter.get_editor_property('parameter_name')) != name or abs(float(parameter.get_editor_property('default_value'))-value) > 1e-6:
            raise RuntimeError('Shared heat scalar default changed: ' + name)
    for name, value in vectors.items():
        parameter = nodes[name]
        color = parameter.get_editor_property('default_value')
        actual = (color.r, color.g, color.b, color.a)
        if str(parameter.get_editor_property('parameter_name')) != name or max(abs(a-b) for a, b in zip(actual, value)) > 1e-6:
            raise RuntimeError('Shared heat vector default changed: ' + name)
    names = ['WorldPosition', *scalars, *vectors]
    if [str(entry.get_editor_property('input_name')) for entry in custom.get_editor_property('inputs')] != names:
        raise RuntimeError('Generated heat custom input names or order changed')
    # UE5.8 GetInputsForMaterialExpression iterates FExpressionInputIterator in
    # input order. Check named links as well as the ownership metadata.
    inputs = EDIT.get_inputs_for_material_expression(material, custom)
    if list(inputs) != [world, *[nodes[name] for name in [*scalars, *vectors]]]:
        raise RuntimeError('Generated heat custom inputs are incomplete or rewired')


def tool_output(nodes):
    return nodes.get('add_existing_emissive', nodes['hotspot'])


def audit_heat_graph(material, nodes, check_material_output=True):
    required = {'hotspot', 'world_position'} | set(SCALARS) | set(VECTORS)
    if set(nodes) not in (required, required | {'add_existing_emissive'}):
        raise RuntimeError('Partial or unexpected tool heat graph; regenerate ordinary imported material first')
    custom = nodes['hotspot']
    audit_custom(material, custom, nodes, SCALARS, VECTORS, CODE, nodes['world_position'])
    final = tool_output(nodes)
    if check_material_output and EDIT.get_material_property_input_node(material, EMISSIVE) != final:
        raise RuntimeError('Tool heat output is disconnected')
    if 'add_existing_emissive' in nodes:
        inputs = list(EDIT.get_inputs_for_material_expression(material, final))
        if len(inputs) != 2 or inputs[0] is None or inputs[0] == custom or inputs[1] != custom:
            raise RuntimeError('Existing source emissive branch is not preserved')
    return len(nodes)


def audit_weld_graph(material, nodes, tool_nodes):
    required = {'hotspot', 'add_existing_emissive'} | set(WELD_SCALARS) | set(WELD_VECTORS)
    if set(nodes) != required:
        raise RuntimeError('Partial or unexpected surface weld graph; regenerate ordinary imported material first')
    audit_custom(material, nodes['hotspot'], nodes, WELD_SCALARS, WELD_VECTORS, WELD_CODE, tool_nodes['world_position'])
    final = nodes['add_existing_emissive']
    if EDIT.get_material_property_input_node(material, EMISSIVE) != final:
        raise RuntimeError('Surface weld output is disconnected')
    if list(EDIT.get_inputs_for_material_expression(material, final)) != [tool_output(tool_nodes), nodes['hotspot']]:
        raise RuntimeError('Existing source/tool emission is not preserved under the weld addition')
    return len(nodes)


def custom_block(material, scalars, vectors, code, world, prefix, y, group):
    custom = node(material, unreal.MaterialExpressionCustom, 'hotspot', 160, y, prefix)
    custom.set_editor_property('description', group + ' / visible PBR emission')
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom_inputs = []
    for name in ['WorldPosition', *scalars, *vectors]:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        custom_inputs.append(entry)
    custom.set_editor_property('inputs', custom_inputs)
    connect(world, '', custom, 'WorldPosition')
    for index, (name, value) in enumerate(scalars.items()):
        parameter = node(material, unreal.MaterialExpressionScalarParameter, name, -700, y+120+index*100, prefix)
        parameter.set_editor_property('parameter_name', name)
        parameter.set_editor_property('default_value', value)
        parameter.set_editor_property('group', group)
        connect(parameter, '', custom, name)
    for index, (name, value) in enumerate(vectors.items()):
        parameter = node(material, unreal.MaterialExpressionVectorParameter, name, -420, y+120+index*100, prefix)
        parameter.set_editor_property('parameter_name', name)
        parameter.set_editor_property('default_value', unreal.LinearColor(*value))
        parameter.set_editor_property('group', group)
        connect(parameter, '', custom, name)
    return custom


def material_result(material, status, before, expressions_before, existing_emission):
    tool_nodes, weld_nodes = owned_nodes(material), owned_nodes(material, WELD_PREFIX)
    count = audit_heat_graph(material, tool_nodes, check_material_output=False) + audit_weld_graph(material, weld_nodes, tool_nodes)
    after = graph_inputs(material)
    if before != after:
        raise RuntimeError('PBR inputs changed during emissive-only augmentation')
    return {'path': material.get_path_name(), 'status': status,
            'generated_nodes': count, 'tool_generated_nodes': len(tool_nodes), 'weld_generated_nodes': len(weld_nodes),
            'expressions_before': expressions_before, 'expressions_after': EDIT.get_num_material_expressions(material),
            'pbr_inputs_unchanged': True, 'pbr_inputs': after,
            'existing_emission_preserved': existing_emission, 'existing_tool_branch_preserved': True,
            'shared_glow_default': 0., 'shared_weld_glow_defaults': [0.] * WELD_PATCH_COUNT}


def augment_material(material):
    if not isinstance(material, unreal.Material):
        raise TypeError('Heat augmentation needs an existing base Material')
    name = material.get_name()
    if not re.fullmatch(r'M_WC_[A-Za-z_][A-Za-z0-9_]*', name) or material.get_path_name() != FOLDER + '/' + name + '.' + name:
        raise ValueError('Heat generator may only edit existing M_WC materials in ' + FOLDER)
    before = graph_inputs(material)
    expressions_before = EDIT.get_num_material_expressions(material)
    tool_nodes, weld_nodes = owned_nodes(material), owned_nodes(material, WELD_PREFIX)
    version = str(ASSETS.get_metadata_tag(material, TAG))
    if weld_nodes:
        if not tool_nodes or version != VERSION:
            raise RuntimeError('Surface weld graph exists without its complete V2 generator state')
        return material_result(material, 'unchanged', before, expressions_before, 'add_existing_emissive' in tool_nodes)

    if tool_nodes:
        # Audit V1 before any mutation. Every V1 expression remains in place.
        audit_heat_graph(material, tool_nodes)
        if version != VERSION_V1:
            raise RuntimeError('Tool heat graph exists without its matching V1 generator version')
        status = 'upgraded_v1'
        old_emissive = tool_output(tool_nodes)
        existing_emission = 'add_existing_emissive' in tool_nodes
    else:
        # Reimport can retain tags after deleting all expressions. A clean PBR
        # graph receives both branches anew without rebuilding any PBR inputs.
        status = 'augmented'
        source_emissive = EDIT.get_material_property_input_node(material, EMISSIVE)
        source_output = str(EDIT.get_material_property_input_node_output_name(material, EMISSIVE))
        world = node(material, unreal.MaterialExpressionWorldPosition, 'world_position', -1000, 480)
        custom = custom_block(material, SCALARS, VECTORS, CODE, world, PREFIX, 500, 'Authored tool heat')
        old_emissive = custom
        if source_emissive:
            old_emissive = node(material, unreal.MaterialExpressionAdd, 'add_existing_emissive', 440, 500)
            connect(source_emissive, source_output, old_emissive, 'A')
            connect(custom, '', old_emissive, 'B')
        tool_nodes = owned_nodes(material)
        existing_emission = bool(source_emissive)

    custom = custom_block(material, WELD_SCALARS, WELD_VECTORS, WELD_CODE, tool_nodes['world_position'], WELD_PREFIX, 1600, 'Authored weld contact heat')
    final = node(material, unreal.MaterialExpressionAdd, 'add_existing_emissive', 700, 1000, WELD_PREFIX)
    connect(old_emissive, '', final, 'A')
    connect(custom, '', final, 'B')
    if not EDIT.connect_material_property(final, '', EMISSIVE):
        raise RuntimeError('Cannot connect augmented source, tool and weld emission')
    result = material_result(material, status, before, expressions_before, existing_emission)
    EDIT.recompile_material(material)
    ASSETS.set_metadata_tag(material, TAG, VERSION)
    ASSETS.set_metadata_tag(material, 'DarkFactoryToolHeatSHA256', CODE_SHA256)
    ASSETS.set_metadata_tag(material, 'DarkFactoryWeldHeatSHA256', WELD_CODE_SHA256)
    ASSETS.set_metadata_tag(material, 'DarkFactoryToolHeatNIR', 'Existing fixed-camera ColorSaturation=0; verify rendered grayscale')
    if not ASSETS.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError('Cannot save heat material: ' + material.get_path_name())
    return result


def augment_materials(materials=None):
    if materials is None:
        materials = [ASSETS.load_asset(path) for path in sorted(ASSETS.list_assets(FOLDER, False, False))
                     if path.rsplit('/', 1)[-1].startswith('M_WC_')]
    materials = list(materials)
    if not materials:
        raise RuntimeError('No existing imported M_WC materials found; import them first')
    rows = [augment_material(material) for material in materials]
    return {'generator': VERSION, 'shader_sha256': SHADER_SHA256, 'tool_shader_sha256': CODE_SHA256,
            'weld_shader_sha256': WELD_CODE_SHA256, 'passed': True,
            'weld_patch_count': WELD_PATCH_COUNT, 'weld_scalar_defaults': WELD_SCALARS, 'weld_vector_defaults': WELD_VECTORS,
            'material_count': len(rows), 'augmented': sum(r['status'] in ('augmented', 'upgraded_v1') for r in rows),
            'upgraded_from_v1': sum(r['status']=='upgraded_v1' for r in rows),
            'unchanged': sum(r['status']=='unchanged' for r in rows), 'materials': rows,
            'scope': 'Authored local PBR emission; no measured blackbody spectrum or temperature calibration'}


def main():
    _, _, args = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    values = {str(k).lower(): str(v) for k, v in args.items()}
    path = Path(values.get('dftoolmaterialreport', str(ROOT / 'artifacts/welding-tool-materials.json')))
    started = time.monotonic()
    try:
        result = augment_materials()
        # A second application must perform no edits or add duplicate nodes.
        replay = augment_materials()
        if replay['augmented'] != 0 or replay['unchanged'] != result['material_count']:
            raise RuntimeError('Heat material augmentation is not idempotent')
        result['replay_unchanged'] = True
        unreal.log('DF_TOOL_MATERIALS_PASSED ' + str(result['material_count']))
    except Exception as error:
        result = {'passed': False, 'error': str(error), 'traceback': traceback.format_exc()}
        unreal.log_error('DF_TOOL_MATERIALS_FAILED ' + str(error))
        raise
    finally:
        result['elapsed_seconds'] = time.monotonic()-started
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()