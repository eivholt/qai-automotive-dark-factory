"""Rebuild the welding spark and arc materials without loading other generators.

Standalone: UnrealEditor-Cmd.exe DarkFactory.uproject -run=pythonscript
-script=<absolute path to this file> -Unattended -NullRHI -nothreading -NoSound

Spark instance custom-data float 0 is an emissive fade (default 1). It crosses
from the vertex stage through VertexInterpolator, so opaque mesh particles can
retain their ordinary depth/custom-depth participation. Runtime retires/scales
spent instances; this material does not introduce translucent particle sorting.
"""
from pathlib import Path
import hashlib
import unreal

ASSETS = unreal.EditorAssetLibrary
EDITING = unreal.MaterialEditingLibrary
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


def material_at(path):
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material is None:
        directory, name = path.rsplit('/', 1)
        material = TOOLS.create_asset(name, directory, unreal.Material, unreal.MaterialFactoryNew())
    if not isinstance(material, unreal.Material):
        raise RuntimeError(f'Cannot create or update base material {path}')
    return material


def expression(material, kind, x, y):
    node = EDITING.create_material_expression(material, kind, x, y)
    if node is None:
        raise RuntimeError(f'Cannot create {kind}')
    return node


def connect(source, output, target, pin):
    if not EDITING.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f'Cannot connect {source.get_class().get_name()} to {pin}')


def rebuild(path, default_color, default_strength, instanced_fade):
    material = material_at(path)
    EDITING.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_SURFACE)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', False)

    color = expression(material, unreal.MaterialExpressionVectorParameter, -700, -120)
    color.set_editor_property('parameter_name', 'SparkColor')
    color.set_editor_property('default_value', unreal.LinearColor(*default_color, 1.0))
    color.set_editor_property('group', 'Welding effect')
    strength = expression(material, unreal.MaterialExpressionScalarParameter, -700, 60)
    strength.set_editor_property('parameter_name', 'EmissiveStrength')
    strength.set_editor_property('default_value', float(default_strength))
    strength.set_editor_property('group', 'Welding effect')
    emission = expression(material, unreal.MaterialExpressionMultiply, -400, -60)
    connect(color, 'RGB', emission, 'A')
    connect(strength, '', emission, 'B')

    if instanced_fade:
        custom_data = expression(material, unreal.MaterialExpressionPerInstanceCustomData, -700, 260)
        custom_data.set_editor_property('data_index', 0)
        custom_data.set_editor_property('const_default_value', 1.0)
        interpolator = expression(material, unreal.MaterialExpressionVertexInterpolator, -400, 260)
        # UE5.8 MaterialExpressionVertexInterpolator.h names the input pin VS.
        connect(custom_data, '', interpolator, 'VS')
        fade = expression(material, unreal.MaterialExpressionMultiply, -100, -40)
        connect(emission, '', fade, 'A')
        connect(interpolator, '', fade, 'B')
        emission = fade

    if not EDITING.connect_material_property(emission, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError(f'Cannot connect emissive output for {path}')
    if instanced_fade:
        EDITING.set_material_usage(material, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
        if not material.get_editor_property('used_with_instanced_static_meshes'):
            raise RuntimeError(f'InstancedStaticMeshes usage missing for {path}')
    EDITING.recompile_material(material)
    if EDITING.get_material_property_input_node(material, unreal.MaterialProperty.MP_EMISSIVE_COLOR) != emission:
        raise RuntimeError(f'Generated emissive graph disconnected for {path}')
    ASSETS.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_welding_effects.py/v1')
    ASSETS.set_metadata_tag(material, 'DarkFactoryGeneratorSHA256', hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
    ASSETS.set_metadata_tag(material, 'DarkFactoryWeldingContract',
                            'SparkColor*EmissiveStrength*VertexInterpolator(PerInstanceCustomData[0],default1)'
                            if instanced_fade else 'SparkColor*EmissiveStrength;no-instance-data')
    if not ASSETS.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f'Failed to save {path}')
    unreal.log(f'DF_WELDING_MATERIAL_READY path={path} instanced_fade={instanced_fade}')


def generate():
    rebuild('/Game/Materials/M_WeldingParticle', (1.0, .55, .12), 16.0, True)
    rebuild('/Game/Materials/M_WeldingArc', (.65, .8, 1.0), 30.0, False)
    unreal.log('DF_WELDING_EFFECTS_READY: opaque unlit particle and arc materials regenerated')


if __name__ == '__main__':
    generate()