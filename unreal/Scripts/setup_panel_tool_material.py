"""Rebuild the private opaque colored dispenser/gripper material."""
import hashlib
from pathlib import Path
import unreal

ASSET='/Game/Materials/M_PanelTool'
assets=unreal.EditorAssetLibrary
editing=unreal.MaterialEditingLibrary
assets.make_directory('/Game/Materials')
material=assets.load_asset(ASSET) if assets.does_asset_exist(ASSET) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_PanelTool','/Game/Materials',unreal.Material,unreal.MaterialFactoryNew())
if not material: raise RuntimeError('Could not create panel-tool material')
editing.delete_all_material_expressions(material)
color=editing.create_material_expression(material,unreal.MaterialExpressionVectorParameter,-350,0)
color.set_editor_property('parameter_name','BaseColor')
color.set_editor_property('default_value',unreal.LinearColor(.1,.12,.15,1))
editing.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
for name,value,prop,y in [('Roughness',.4,unreal.MaterialProperty.MP_ROUGHNESS,160),('Metallic',0.,unreal.MaterialProperty.MP_METALLIC,320)]:
 parameter=editing.create_material_expression(material,unreal.MaterialExpressionScalarParameter,-350,y)
 parameter.set_editor_property('parameter_name',name);parameter.set_editor_property('default_value',value)
 editing.connect_material_property(parameter,'',prop)
editing.set_material_usage(material,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
editing.recompile_material(material)
assets.set_metadata_tag(material,'DarkFactoryGenerator','setup_panel_tool_material.py/colored-tools-v1')
assets.set_metadata_tag(material,'DarkFactorySourceSHA256',hashlib.sha256(Path(__file__).read_bytes()).hexdigest())
if not assets.save_loaded_asset(material,only_if_is_dirty=False): raise RuntimeError('Could not save panel-tool material')
unreal.log('DF_PANEL_TOOL_MATERIAL_READY opaque=true thermal_profiles=shared')
