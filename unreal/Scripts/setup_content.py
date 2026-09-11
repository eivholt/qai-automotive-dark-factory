"""Reproducible UE5.8 content generation via UnrealEditor-Cmd -run=pythonscript.

M_Factory and the empty map are created once. The four night-vision materials
are regenerated IN PLACE on every run from the canonical unreal/Shaders files.
Fast defaults compile one source sample; Quality variants compile five for halo.
No Time node is used: the caller sets NVFrame for animation or fixed capture.
"""
from pathlib import Path
import hashlib
import unreal

MATERIAL = '/Game/Materials/M_Factory'
MAP = '/Game/Maps/DarkFactory'
SHADER_ROOT = Path(__file__).resolve().parents[1] / 'Shaders'
NV_DEFAULTS = {
    'NVGain': 1.35,
    'NVGrain': 0.035,
    'NVVignette': 0.30,
    'NVPhosphor': 1.0,
    'NVFrame': 0.0,
    'NVSeed': 17.0,
    'NVHalo': 0.12,
}
assets = unreal.EditorAssetLibrary
editing = unreal.MaterialEditingLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()


def expression(material, kind, x, y):
    node = editing.create_material_expression(material, kind, x, y)
    if not node:
        raise RuntimeError(f'Cannot create material expression {kind}')
    return node


def connect(source, source_output, target, target_input):
    if not editing.connect_material_expressions(source, source_output, target, target_input):
        raise RuntimeError(f'Cannot connect {source_output!r} to {target_input!r}')


def material_at(path):
    if assets.does_asset_exist(path):
        material = assets.load_asset(path)
        if not isinstance(material, unreal.Material):
            raise RuntimeError(f'{path} exists but is not a base Material')
        return material
    folder, name = path.rsplit('/', 1)
    material = asset_tools.create_asset(name, folder, unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f'Cannot create {path}')
    return material


def rebuild_night_vision(path, source_name, is_ui, enable_halo=False):
    common = (SHADER_ROOT / 'NightVisionCommon.ush').read_text(encoding='utf-8-sig')
    body = (SHADER_ROOT / source_name).read_text(encoding='utf-8-sig')
    if 'struct FDarkFactoryNV' not in common or 'return NV.Display' not in body:
        raise RuntimeError(f'Night vision shader sources missing or incomplete: {source_name}')
    variant = 'quality' if enable_halo else 'fast'
    code = f'#define DF_NV_HALO {int(enable_halo)}\n' + common + '\n' + body
    material = material_at(path)
    editing.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI if is_ui else unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    if not is_ui:
        # UE5.8 BlendableInterface.h: renamed from the deprecated AfterTonemapping.
        material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
        material.set_editor_property('blendable_priority', 10)
    custom = expression(material, unreal.MaterialExpressionCustom, 80, 0)
    custom.set_editor_property('description', f'DarkFactory display-only NV / {variant} / canonical HLSL')
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    input_names = (['SensorImage', 'UV'] if is_ui else ['SceneColor']) + list(NV_DEFAULTS)
    inputs = []
    for name in input_names:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    custom.set_editor_property('inputs', inputs)

    if is_ui:
        image = expression(material, unreal.MaterialExpressionTextureObjectParameter, -520, -220)
        image.set_editor_property('parameter_name', 'SensorImage')
        # Valid engine resource compiles the texture-object parameter before the
        # runtime replaces it with the direct SRGB render target or grayscale texture.
        fallback = assets.load_asset('/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture')
        if not fallback:
            raise RuntimeError('Engine WhiteSquareTexture is missing; cannot compile the NV UI material')
        image.set_editor_property('texture', fallback)
        image.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        connect(image, '', custom, 'SensorImage')
        uv = expression(material, unreal.MaterialExpressionTextureCoordinate, -520, -80)
        connect(uv, '', custom, 'UV')
    else:
        scene = expression(material, unreal.MaterialExpressionSceneTexture, -520, -160)
        scene.set_editor_property('scene_texture_id', unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
        scene.set_editor_property('filtered', True)
        connect(scene, 'Color', custom, 'SceneColor')

    for index, (name, value) in enumerate(NV_DEFAULTS.items()):
        parameter = expression(material, unreal.MaterialExpressionScalarParameter, -520, 130 + index * 95)
        parameter.set_editor_property('parameter_name', name)
        parameter.set_editor_property('default_value', value)
        parameter.set_editor_property('group', 'Night vision display')
        connect(parameter, '', custom, name)
    if not editing.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError(f'Cannot connect generated final color in {path}')
    editing.recompile_material(material)
    source_hash = hashlib.sha256(code.encode('utf-8')).hexdigest()
    assets.set_metadata_tag(material, 'DarkFactoryShaderSHA256', source_hash)
    assets.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_content.py/night-vision-v2')
    assets.set_metadata_tag(material, 'DarkFactoryNVVariant', variant)
    assets.set_metadata_tag(material, 'DarkFactoryNVTextureSamples', '5' if enable_halo else '1')
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f'Failed to save generated material {path}')
    unreal.log(f'DF_NV_MATERIAL_READY path={path} variant={variant} source_sha256={source_hash}')



def thermal_fallback():
    """A linear floating texture-object default; runtime binds the actual LUT/RT."""
    path = '/Game/Materials/RT_ThermalFallback'
    if assets.does_asset_exist(path):
        target = assets.load_asset(path)
        if not isinstance(target, unreal.TextureRenderTarget2D):
            raise RuntimeError(f'{path} is not a TextureRenderTarget2D')
    else:
        factory = unreal.TextureRenderTargetFactoryNew()
        target = asset_tools.create_asset('RT_ThermalFallback', '/Game/Materials', unreal.TextureRenderTarget2D, factory)
    if not target:
        raise RuntimeError('Could not create the thermal linear fallback texture')
    target.set_editor_property('size_x', 256)
    target.set_editor_property('size_y', 12)
    # UE5.8 PostEditChangeProperty sets forceLinearGamma=true for this format.
    target.set_editor_property('render_target_format', unreal.TextureRenderTargetFormat.RTF_RGBA32F)
    target.set_editor_property('target_gamma', 1.0)
    target.set_editor_property('clear_color', unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    target.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    target.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    target.set_editor_property('filter', unreal.TextureFilter.TF_NEAREST)
    if not assets.save_loaded_asset(target, only_if_is_dirty=False):
        raise RuntimeError('Could not save thermal fallback texture')
    return target


def rebuild_thermal(path, source_name, is_ui, fallback):
    common = (SHADER_ROOT / 'ThermalCommon.ush').read_text(encoding='utf-8-sig')
    body = (SHADER_ROOT / source_name).read_text(encoding='utf-8-sig')
    if 'struct FDarkFactoryThermal' not in common or 'return Thermal.Palette' not in body:
        raise RuntimeError(f'Thermal shader sources missing or incomplete: {source_name}')
    code = common + '\n' + body
    material = material_at(path)
    editing.delete_all_material_expressions(material)
    material.set_editor_property('material_domain', unreal.MaterialDomain.MD_UI if is_ui else unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    if not is_ui:
        material.set_editor_property('blendable_location', unreal.BlendableLocation.BL_SCENE_COLOR_AFTER_TONEMAPPING)
        material.set_editor_property('blendable_priority', 20)
    custom = expression(material, unreal.MaterialExpressionCustom, 80, 0)
    custom.set_editor_property('description', 'DarkFactory thermal / spatial surface temperature field')
    custom.set_editor_property('code', code)
    custom.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    defaults = {'ThermalPalette': 0.0} if is_ui else {
        'ThermalAmbientC': 24.0,
        'ThermalMinC': 20.0,
        'ThermalMaxC': 120.0,
        'ThermalDepthToleranceCm': 0.1,
        'ThermalPalette': 0.0,
        'ThermalSpatialEnabled': 1.0,
        'ThermalTransferEnabled': 1.0,
        'ThermalDebugIds': 0.0,
    }
    names = (['SensorImage', 'UV'] if is_ui else ['SceneDepthCm', 'SurfaceDepthCm', 'SurfaceId', 'ThermalLUT', 'WorldPositionCm']) + list(defaults)
    inputs = []
    for name in names:
        entry = unreal.CustomInput()
        entry.set_editor_property('input_name', name)
        inputs.append(entry)
    custom.set_editor_property('inputs', inputs)
    texture_name = 'SensorImage' if is_ui else 'ThermalLUT'
    texture = expression(material, unreal.MaterialExpressionTextureObjectParameter, -520, -300)
    texture.set_editor_property('parameter_name', texture_name)
    texture.set_editor_property('texture', fallback)
    texture.set_editor_property('sampler_type', unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    connect(texture, '', custom, texture_name)
    if is_ui:
        uv = expression(material, unreal.MaterialExpressionTextureCoordinate, -520, -130)
        connect(uv, '', custom, 'UV')
    else:
        # Default absolute position is reconstructed from SceneDepth in a PP
        # material; Unreal handles viewport offsets and resolution differences.
        world = expression(material, unreal.MaterialExpressionWorldPosition, -850, 380)
        connect(world, 'XYZ', custom, 'WorldPositionCm')
        for index, (name, texture_id) in enumerate((
            ('SceneDepthCm', unreal.SceneTextureId.PPI_SCENE_DEPTH),
            ('SurfaceDepthCm', unreal.SceneTextureId.PPI_CUSTOM_DEPTH),
            ('SurfaceId', unreal.SceneTextureId.PPI_CUSTOM_STENCIL),
        )):
            scene = expression(material, unreal.MaterialExpressionSceneTexture, -850, -120 + index * 150)
            scene.set_editor_property('scene_texture_id', texture_id)
            scene.set_editor_property('filtered', False)
            connect(scene, 'Color', custom, name)
    for index, (name, value) in enumerate(defaults.items()):
        parameter = expression(material, unreal.MaterialExpressionScalarParameter, -520, 350 + index * 100)
        parameter.set_editor_property('parameter_name', name)
        parameter.set_editor_property('default_value', value)
        parameter.set_editor_property('group', 'Thermal sensor/display')
        connect(parameter, '', custom, name)
    if not editing.connect_material_property(custom, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        raise RuntimeError(f'Cannot connect thermal output in {path}')
    editing.recompile_material(material)
    source_hash = hashlib.sha256(code.encode('utf-8')).hexdigest()
    assets.set_metadata_tag(material, 'DarkFactoryShaderSHA256', source_hash)
    assets.set_metadata_tag(material, 'DarkFactoryGenerator', 'setup_content.py/thermal-transfer-v3')
    assets.set_metadata_tag(material, 'DarkFactoryThermalLUT', '256x12;row0=apparentFitMinMidMaxC,registered;rows1-3=worldToNormalizedLocal;row4=edgeFalloff,uniformC,coolingRetention,edgeRetainedFraction;rows5-10=faceCenterUV,invRadiusSquared,deltaSurfaceC;row11=edgeSurfaceC,coreSurfaceC,fitMinC,invFitSpan')
    if not assets.save_loaded_asset(material, only_if_is_dirty=False):
        raise RuntimeError(f'Failed to save generated thermal material {path}')
    unreal.log(f'DF_THERMAL_MATERIAL_READY path={path} source_sha256={source_hash}')

if not assets.does_asset_exist(MATERIAL):
    material = material_at(MATERIAL)
    color = expression(material, unreal.MaterialExpressionVectorParameter, -400, 0)
    color.set_editor_property('parameter_name', 'BaseColor')
    color.set_editor_property('default_value', unreal.LinearColor(0.5, 0.5, 0.5, 1))
    roughness = expression(material, unreal.MaterialExpressionScalarParameter, -400, 180)
    roughness.set_editor_property('parameter_name', 'Roughness')
    roughness.set_editor_property('default_value', 0.6)
    editing.connect_material_property(color, '', unreal.MaterialProperty.MP_BASE_COLOR)
    editing.connect_material_property(roughness, '', unreal.MaterialProperty.MP_ROUGHNESS)
    editing.recompile_material(material)
    assets.save_loaded_asset(material)

rebuild_night_vision('/Game/Materials/M_PP_NightVision', 'NightVisionPostProcess.usf', is_ui=False)
rebuild_night_vision('/Game/Materials/M_UI_NightVision', 'NightVisionSensorUI.usf', is_ui=True)
rebuild_night_vision('/Game/Materials/M_PP_NightVisionQuality', 'NightVisionPostProcess.usf', is_ui=False, enable_halo=True)
rebuild_night_vision('/Game/Materials/M_UI_NightVisionQuality', 'NightVisionSensorUI.usf', is_ui=True, enable_halo=True)

thermal_texture = thermal_fallback()
rebuild_thermal('/Game/Materials/M_PP_Thermal', 'ThermalPostProcess.usf', is_ui=False, fallback=thermal_texture)
rebuild_thermal('/Game/Materials/M_UI_Thermal', 'ThermalSensorUI.usf', is_ui=True, fallback=thermal_texture)

if not assets.does_asset_exist(MAP):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.new_level(MAP):
        raise RuntimeError('Unable to create empty DarkFactory map')
    levels.save_current_level()
unreal.log('DF_CONTENT_READY: factory assets, night-vision and thermal materials ready')



