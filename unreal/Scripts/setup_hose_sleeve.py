"""Optional generated hose mesh; legacy fixtures retain the original cylinder."""
from pathlib import Path
import unreal
source=Path(__file__).resolve().parents[2]/'assets/derived/hose_sleeve/SM_HoseSleeve.fbx'
if source.exists():
    task=unreal.AssetImportTask();task.filename=str(source);task.destination_path='/Game/FactoryContext'
    task.automated=True;task.replace_existing=True;task.save=True
    options=unreal.FbxImportUI();options.import_mesh=True;options.import_materials=False;options.import_textures=False
    options.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
    options.static_mesh_import_data.combine_meshes=True
    options.static_mesh_import_data.auto_generate_collision=False
    task.options=options
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
    unreal.log('DF_HOSE_SLEEVE_READY')
