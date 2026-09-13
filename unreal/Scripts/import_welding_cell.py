"""Import purchased/derived welding-cell FBX assets without creating editor actors.

Run through import_welding_cell.ps1. Original FBX/JPEG sources are read-only.
The prepared FBX must contain identity nodes with geometry local to manifest pivots;
scene.json owns parent-relative placement and animation, not the importer's actors.

Prepared manifest: {version: 1, fbx: 'prepared.fbx', meshes: [{id, fbx_node: id,
expected_bounds_min_cm: [x,y,z], expected_bounds_max_cm: [x,y,z]}]}. Optional
materials entries contain id, base_color (linear RGB), metallic, roughness, and
base_color_texture (path relative to the manifest). All identifiers are stable
Unreal names. Imported local bounds must match the manifest within 0.1 cm.
"""
from pathlib import Path
import hashlib
import json
import re
import runpy
import time
import traceback
import unreal

ROOT = Path(__file__).resolve().parents[2]
SOURCE_DIR = ROOT / 'assets' / 'Autodesk_FBX_2009'
DEFAULT_SOURCE = SOURCE_DIR / 'Car_Body_Welding_Robotics_Cell.FBX'
PREFIX = 'Car_Body_Welding_Robotics_Cell_'
MATLIB = unreal.MaterialEditingLibrary
ASSETS = unreal.EditorAssetLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def sha256(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def source_hashes():
    return {str(path.relative_to(ROOT)): sha256(path)
            for path in sorted(SOURCE_DIR.iterdir()) if path.is_file()}


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + '\n', encoding='utf-8')


def vec(value):
    return [float(value.x), float(value.y), float(value.z)]


def canonical_material(name):
    name = str(name).split('\x00')[0]
    name = name.removeprefix(PREFIX)
    name = re.sub(r'_ncl\d+_\d+$', '', name)
    return name


def params():
    _, _, values = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    return {str(key).lower(): str(value) for key, value in values.items()}


def expression(material, cls, x, y):
    return MATLIB.create_material_expression(material, cls, x, y)


def constant(material, value, prop, y):
    node = expression(material, unreal.MaterialExpressionConstant, -220, y)
    node.set_editor_property('r', float(value))
    MATLIB.connect_material_property(node, '', prop)


def import_texture(path, name):
    destination = '/Game/WeldingCell/Textures'
    task = unreal.AssetImportTask()
    task.set_editor_property('filename', str(path))
    task.set_editor_property('destination_path', destination)
    task.set_editor_property('destination_name', name)
    task.set_editor_property('automated', True)
    task.set_editor_property('replace_existing', True)
    task.set_editor_property('save', True)
    TOOLS.import_asset_tasks([task])
    texture = ASSETS.load_asset(destination + '/' + name)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError('Texture import failed: ' + str(path))
    texture.set_editor_property('srgb', True)
    ASSETS.save_loaded_asset(texture)
    return texture


def default_materials():
    # Standard PBR reconstruction; the seller's V-Ray/Scanline shader graphs are
    # unavailable in Unreal. Texture basenames resolve explicitly beside the FBX.
    colors = {
        # Rough galvanized-steel preview keeps body panels readable without an
        # environment reflection capture. Visible/NIR reflectance is an artistic
        # approximation, not a measured optical or spectral material property.
        'chrome': ([.38, .42, .45], .55, .42),
        'robots': ([.76, .11, .004], .35, .38),
        'robots_blue': ([.035, .098, .286], .25, .42),
        'robots_plastic': ([.24, .24, .24], 0., .52),
        'robots_plastic_black': ([.025, .025, .025], 0., .48),
        'robots_plastic_gray': ([.35, .35, .35], 0., .52),
        'robots_screen': ([.15, .15, .15], 0., .32),
        'robots_tiles': ([.5, .5, .5], 0., .75),
        'robots_white': ([.7, .7, .7], .15, .48),
        'robots_yellow': ([.9, .55, .015], .15, .42),
    }
    definitions = [{'id': name, 'base_color': color, 'metallic': metal, 'roughness': rough}
                   for name, (color, metal, rough) in colors.items()]
    textures = {
        'robots': 'Car_Body_Welding_Robotics_Cell_robots.jpg',
        'robots_tiles': 'Car_Body_Welding_Robotics_Cell_tiles.jpg',
        'robots_screen': 'Automation_Robotic_Bag_Palletizing_Cel_screen.jpg',
    }
    for definition in definitions:
        if definition['id'] in textures:
            definition['base_color_texture'] = str(SOURCE_DIR / textures[definition['id']])
    return definitions


def build_materials(definitions, manifest_directory):
    materials = {}
    for definition in definitions:
        key = canonical_material(definition['id'])
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', key):
            raise ValueError('Unsafe material identifier: ' + key)
        name = 'M_WC_' + key
        path = '/Game/WeldingCell/Materials/' + name
        material = ASSETS.load_asset(path)
        if material is None:
            material = TOOLS.create_asset(name, '/Game/WeldingCell/Materials', unreal.Material, unreal.MaterialFactoryNew())
        if not isinstance(material, unreal.Material):
            raise TypeError('Generated material path has another asset type: ' + path)
        MATLIB.delete_all_material_expressions(material)
        material.set_editor_property('two_sided', bool(definition.get('two_sided', False)))
        texture_path = definition.get('base_color_texture')
        if texture_path:
            texture_path = Path(texture_path)
            if not texture_path.is_absolute():
                texture_path = manifest_directory / texture_path
            texture = import_texture(texture_path.resolve(), 'T_WC_' + key)
            node = expression(material, unreal.MaterialExpressionTextureSample, -500, -80)
            node.set_editor_property('texture', texture)
            node.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
            MATLIB.connect_material_property(node, 'RGB', unreal.MaterialProperty.MP_BASE_COLOR)
        else:
            color = definition.get('base_color', [.35, .35, .35])
            node = expression(material, unreal.MaterialExpressionConstant3Vector, -300, -80)
            node.set_editor_property('constant', unreal.LinearColor(float(color[0]), float(color[1]), float(color[2]), 1.))
            MATLIB.connect_material_property(node, '', unreal.MaterialProperty.MP_BASE_COLOR)
        constant(material, definition.get('metallic', 0.), unreal.MaterialProperty.MP_METALLIC, 120)
        constant(material, definition.get('roughness', .5), unreal.MaterialProperty.MP_ROUGHNESS, 220)
        MATLIB.recompile_material(material)
        ASSETS.save_loaded_asset(material)
        materials[key] = material
    return materials


def import_meshes(source, destination, prepared):
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX 0')
    unreal.SystemLibrary.execute_console_command(world, 'Interchange.FeatureFlags.Import.FBX.ToLevel 0')
    options = unreal.FbxImportUI()
    for key, value in {
        'automated_import_should_detect_type': False,
        'mesh_type_to_import': unreal.FBXImportType.FBXIT_STATIC_MESH,
        'import_as_skeletal': False, 'import_mesh': True,
        'import_animations': False, 'create_physics_asset': False,
        'import_materials': False, 'import_textures': False,
    }.items():
        options.set_editor_property(key, value)
    settings = options.get_editor_property('static_mesh_import_data')
    for key, value in {
        'combine_meshes': False, 'transform_vertex_to_absolute': not prepared,
        'bake_pivot_in_vertex': False, 'convert_scene': True,
        'convert_scene_unit': True, 'force_front_x_axis': False,
        'import_uniform_scale': 1., 'auto_generate_collision': False,
        'generate_lightmap_u_vs': False, 'build_nanite': False,
        'remove_degenerates': True,
        'normal_import_method': unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS,
    }.items():
        settings.set_editor_property(key, value)
    task = unreal.AssetImportTask()
    for key, value in {
        'filename': str(source), 'destination_path': destination,
        # FFbxImporter::MakeNameForMesh uses node names without a prefix for None.
        'destination_name': 'None' if prepared else source.stem,
        'automated': True, 'replace_existing': True,
        'replace_existing_settings': True, 'save': True,
        'factory': unreal.FbxFactory(), 'options': options,
    }.items():
        task.set_editor_property(key, value)
    unreal.log('DF_WELDING_IMPORT_BEGIN ' + str(source))
    TOOLS.import_asset_tasks([task])
    return list(task.get_editor_property('imported_object_paths'))


def audit_meshes(destination, materials):
    meshes = []
    for path in ASSETS.list_assets(destination, True, False):
        mesh = ASSETS.load_asset(path)
        if not isinstance(mesh, unreal.StaticMesh):
            continue
        slots = []
        for index, slot in enumerate(mesh.get_editor_property('static_materials')):
            source_slot = str(slot.get_editor_property('imported_material_slot_name'))
            if not source_slot or source_slot == 'None':
                source_slot = str(slot.material_slot_name)
            key = canonical_material(source_slot)
            material = materials.get(key)
            if material is None:
                raise RuntimeError('Unmatched material slot ' + key + ' on ' + path)
            mesh.set_material(index, material)
            slots.append({'index': index, 'source_name': source_slot,
                          'id': key, 'material': material.get_path_name()})
        ASSETS.save_loaded_asset(mesh)
        bounds = mesh.get_bounds()
        item = {'path': mesh.get_path_name(), 'name': mesh.get_name(),
                'bounds_origin_cm': vec(bounds.origin), 'bounds_extent_cm': vec(bounds.box_extent),
                'lod_count': mesh.get_num_lods(),
                'lod0_vertices': mesh.get_num_vertices(0),
                'lod0_triangles': mesh.get_num_triangles(0),
                'lod0_uv_channels': mesh.get_num_tex_coords(0),
                'nanite_enabled': bool(mesh.get_editor_property('nanite_settings').enabled),
                'materials': slots}
        meshes.append(item)
    if not meshes:
        raise RuntimeError('No StaticMesh assets imported')
    return meshes


def audit_materials(materials, definitions):
    """Inspect authored graph links: NullRHI has no compiled texture-uniform cache."""
    result = []
    for definition in definitions:
        key = canonical_material(definition['id'])
        material = materials[key]
        if not isinstance(material, unreal.Material):
            raise RuntimeError('Missing generated material: ' + key)
        node = MATLIB.get_material_property_input_node(material, unreal.MaterialProperty.MP_BASE_COLOR)
        if node is None:
            raise RuntimeError('Material base color is disconnected: ' + key)
        final_expression = node.get_class().get_name()
        # A generated witness lerp retains the untouched source as input A.
        # Audit the exact owned wrapper before following it to the source texture.
        witness_wrapper = str(node.get_editor_property('desc')) == 'DF_WELD_WITNESS_V1/color_result'
        if witness_wrapper:
            links = list(MATLIB.get_inputs_for_material_expression(material, node))
            if not isinstance(node, unreal.MaterialExpressionLinearInterpolate) or len(links) != 3:
                raise RuntimeError('Invalid weld-witness base color wrapper: ' + key)
            source, dark, mask = links
            dark_links = list(MATLIB.get_inputs_for_material_expression(material, dark)) if dark else []
            if source is None or not isinstance(dark, unreal.MaterialExpressionMultiply) or not dark_links or dark_links[0] != source or not mask or str(mask.get_editor_property('desc')) != 'DF_WELD_WITNESS_V1/hotspot':
                raise RuntimeError('Weld witness does not preserve source base color: ' + key)
            node = source
        item = {'id': key, 'path': material.get_path_name(), 'textures': [],
                'base_color_expression': node.get_class().get_name(),
                'final_base_color_expression': final_expression, 'witness_wrapper': witness_wrapper,
                'authored_base_color_connected': True}
        if definition.get('base_color_texture'):
            if not isinstance(node, unreal.MaterialExpressionTextureSample):
                raise RuntimeError('Expected texture directly connected to base color: ' + key)
            texture = node.get_editor_property('texture')
            if not isinstance(texture, unreal.Texture2D):
                raise RuntimeError('Material texture reference missing: ' + key)
            item['textures'] = [texture.get_path_name()]
            item['texture_size'] = [texture.blueprint_get_size_x(), texture.blueprint_get_size_y()]
            item['texture_srgb'] = bool(texture.get_editor_property('srgb'))
        result.append(item)
    return result


def validate_prepared(manifest, destination):
    definitions = manifest.get('meshes', [])
    if not definitions:
        raise ValueError('Prepared manifest has no meshes')
    ids = [definition['id'] for definition in definitions]
    if len(set(ids)) != len(ids):
        raise ValueError('Duplicate prepared mesh identifiers')
    actual = {}
    for path in ASSETS.list_assets(destination, True, False):
        asset = ASSETS.load_asset(path)
        if isinstance(asset, unreal.StaticMesh):
            actual[asset.get_name()] = asset
    missing = sorted(set(ids) - set(actual))
    if missing:
        raise RuntimeError('Prepared meshes missing (imported names ' + repr(sorted(actual)) + '): ' + repr(missing))
    audit = []
    for definition in definitions:
        identifier = definition['id']
        if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', identifier):
            raise ValueError('Unsafe mesh identifier: ' + identifier)
        if definition.get('fbx_node', identifier) != identifier:
            raise ValueError('Prepared FBX node must equal its stable mesh id')
        mesh = actual[identifier]
        bounds = mesh.get_bounds()
        origin, extent = vec(bounds.origin), vec(bounds.box_extent)
        low = [a-b for a, b in zip(origin, extent)]
        high = [a+b for a, b in zip(origin, extent)]
        expected_low = definition.get('expected_bounds_min_cm')
        expected_high = definition.get('expected_bounds_max_cm')
        if expected_low is None or expected_high is None:
            raise ValueError('Prepared manifest needs expected local bounds: ' + identifier)
        error = max(abs(a-b) for a, b in zip(low + high, expected_low + expected_high))
        if error > .1:
            raise RuntimeError('Prepared local bounds mismatch ' + identifier + ': ' + str(error) + ' cm')
        audit.append({'id': identifier, 'asset_path': mesh.get_path_name(), 'bounds_max_error_cm': error})
    return {'expected_meshes': len(ids), 'validated_meshes': len(audit),
            'extra_meshes': sorted(set(actual) - set(ids)), 'bounds_tolerance_cm': .1,
            'meshes': audit}

def main():
    args = params()
    mode = args.get('dfimportmode', 'raw')
    if mode not in ('raw', 'prepared'):
        raise ValueError('DFImportMode must be raw or prepared')
    manifest_path = Path(args.get('dfimportmanifest', str(ROOT / 'assets/derived/welding_cell/manifest.json')))
    manifest = json.loads(manifest_path.read_text(encoding='utf-8')) if mode == 'prepared' else {}
    source = Path(args.get('dfimportsource', str(manifest_path.parent / manifest.get('fbx', 'prepared.fbx')) if mode == 'prepared' else str(DEFAULT_SOURCE))).resolve()
    destination = args.get('dfimportdestination', '/Game/WeldingCell/Meshes' if mode == 'prepared' else '/Game/WeldingCell/Raw').rstrip('/')
    if destination not in ('/Game/WeldingCell/Raw', '/Game/WeldingCell/Meshes'):
        raise ValueError('Importer may write only /Game/WeldingCell/Raw or /Game/WeldingCell/Meshes')
    report_path = Path(args.get('dfimportreport', str(ROOT / 'artifacts/asset-inspection' / ('welding_' + mode + '_import.json'))))
    started = time.monotonic()
    result = {'status': 'starting', 'mode': mode, 'source': str(source), 'destination': destination,
              'source_sha256_before': source_hashes(), 'settings': {
                  'factory': 'FbxFactory', 'combine_meshes': False,
                  'transform_vertex_to_absolute': mode == 'raw', 'convert_scene': True,
                  'convert_scene_unit': True, 'build_nanite': False,
                  'import_materials': False, 'import_textures': False,
                  'normal_import_method': 'import_normals', 'remove_degenerates': True,
                  'auto_generate_collision': False, 'generate_lightmap_uvs': False},
              'material_note': 'Standard PBR reconstruction with explicit adjacent JPEG links; source V-Ray/Scanline graphs and procedural Map115 are not reproduced. Chrome/body panels use an approximate rough galvanized-steel preview (linear base color .38/.42/.45, metallic .55, roughness .42) for readable dim-view silhouettes; visible/NIR reflectance is not measured or spectral.'}
    write_json(report_path, result)
    try:
        if not source.is_file():
            raise FileNotFoundError(source)
        result['import_source_sha256'] = sha256(source)
        result['status'] = 'importing_meshes'
        write_json(report_path, result)
        if args.get('dfimportskipmesh', '0') == '1':
            result['task_imported_object_paths'] = []
            result['mesh_import_skipped'] = True
        else:
            result['task_imported_object_paths'] = import_meshes(source, destination, mode == 'prepared')
        result['status'] = 'building_materials'
        write_json(report_path, result)
        if mode == 'prepared':
            result['prepared_validation'] = validate_prepared(manifest, destination)
        definitions = manifest.get('materials') or default_materials()
        if args.get('dfimportskipmaterials', '0') == '1':
            materials = {canonical_material(d['id']): ASSETS.load_asset('/Game/WeldingCell/Materials/M_WC_' + canonical_material(d['id'])) for d in definitions}
        else:
            materials = build_materials(definitions, manifest_path.parent)
        # Rebuilding PBR expressions removes the optional runtime heat branch.
        # Reapply it from source even when previous asset metadata survives.
        tool_materials = runpy.run_path(str(Path(__file__).with_name('setup_welding_tool_materials.py')))
        result['tool_heat_materials'] = tool_materials['augment_materials'](materials.values())
        witness_materials = runpy.run_path(str(Path(__file__).with_name('setup_weld_witness.py')))
        result['weld_witness_materials'] = witness_materials['augment_materials'](materials.values())
        cabinet_material = runpy.run_path(str(Path(__file__).with_name('setup_control_cabinet.py')))
        result['control_cabinet_material'] = cabinet_material['generate']()
        meshes = audit_meshes(destination, materials)
        result['meshes'] = meshes
        result['materials'] = audit_materials(materials, definitions)
        result['counts'] = {'meshes': len(meshes), 'materials': len(materials),
                            'textures': len(set(texture for entry in result['materials'] for texture in entry['textures'])),
                            'lod0_triangles': sum(mesh['lod0_triangles'] for mesh in meshes)}
        result['verified_nanite_disabled'] = all(not mesh['nanite_enabled'] for mesh in meshes)
        if mode == 'raw':
            if len(meshes) != 10:
                raise RuntimeError('Expected exactly ten raw material-group meshes')
            low = [min(mesh['bounds_origin_cm'][axis] - mesh['bounds_extent_cm'][axis] for mesh in meshes) for axis in range(3)]
            high = [max(mesh['bounds_origin_cm'][axis] + mesh['bounds_extent_cm'][axis] for mesh in meshes) for axis in range(3)]
            result['assembled_identity_bounds_cm'] = {'min': low, 'max': high, 'size': [b-a for a, b in zip(low, high)]}
        if not ASSETS.save_directory('/Game/WeldingCell', False, True):
            raise RuntimeError('Failed saving imported assets')
        result['status'] = 'imported_and_audited'
    except Exception as error:
        result.update(status='error', error=str(error), traceback=traceback.format_exc())
        unreal.log_error('DF_WELDING_IMPORT_FAILED ' + str(error))
        raise
    finally:
        result['source_sha256_after'] = source_hashes()
        result['source_files_unchanged'] = result['source_sha256_before'] == result['source_sha256_after']
        result['elapsed_seconds'] = time.monotonic() - started
        write_json(report_path, result)
        unreal.log('DF_WELDING_IMPORT_REPORT ' + str(report_path))
    if not result['source_files_unchanged']:
        raise RuntimeError('Original source hash changed')
    unreal.log('DF_WELDING_IMPORT_PASSED ' + json.dumps(result['counts']))


if __name__ == '__main__':
    main()