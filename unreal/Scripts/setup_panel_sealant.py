"""Generate one private masked PBR sealant material; UV.x is deposited arc length."""
from pathlib import Path
import hashlib,json
import unreal
ROOT=Path(__file__).resolve().parents[2]
ASSETS=unreal.EditorAssetLibrary
EDIT=unreal.MaterialEditingLibrary
PATH='/Game/Materials/M_PanelSealant'
CODE='return saturate((DepositProgress - UV.x) * 400.0 + 0.5);'

def generate():
    material=ASSETS.load_asset(PATH) if ASSETS.does_asset_exist(PATH) else unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_PanelSealant','/Game/Materials',unreal.Material,unreal.MaterialFactoryNew())
    if not isinstance(material,unreal.Material): raise RuntimeError('Expected sealant material')
    signature=hashlib.sha256((CODE+'purple-753bbd/roughness.34/specular.48/v1').encode()).hexdigest()
    if ASSETS.get_metadata_tag(material,'DarkFactorySealantSHA256')==signature:
        return {'material':PATH,'status':'unchanged','sha256':signature}
    EDIT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property('two_sided',False)
    material.set_editor_property('opacity_mask_clip_value',.5)
    def node(kind,x,y): return EDIT.create_material_expression(material,kind,x,y)
    def output(n,name,prop):
        if not EDIT.connect_material_property(n,name,prop): raise RuntimeError('Cannot connect sealant output')
    color=node(unreal.MaterialExpressionVectorParameter,-500,-300)
    color.set_editor_property('parameter_name','BaseColor')
    # Saturated Snapdragon-inspired violet; authored display color, not measured NIR reflectance.
    def linear(v): return ((v/255+.055)/1.055)**2.4 if v>10 else v/255/12.92
    color.set_editor_property('default_value',unreal.LinearColor(linear(117),linear(59),linear(189),1.))
    output(color,'RGB',unreal.MaterialProperty.MP_BASE_COLOR)
    for i,(name,value,prop) in enumerate([('Roughness',.34,unreal.MaterialProperty.MP_ROUGHNESS),('Specular',.48,unreal.MaterialProperty.MP_SPECULAR)]):
        n=node(unreal.MaterialExpressionScalarParameter,-500,-100+i*120);n.set_editor_property('parameter_name',name);n.set_editor_property('default_value',value);output(n,'',prop)
    progress=node(unreal.MaterialExpressionScalarParameter,-500,300);progress.set_editor_property('parameter_name','DepositProgress');progress.set_editor_property('default_value',1.)
    uv=node(unreal.MaterialExpressionTextureCoordinate,-500,450)
    mask=node(unreal.MaterialExpressionCustom,0,300);mask.set_editor_property('code',CODE);mask.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    inputs=[]
    for name in ['UV','DepositProgress']:
        item=unreal.CustomInput();item.set_editor_property('input_name',name);inputs.append(item)
    mask.set_editor_property('inputs',inputs)
    for source,name in [(uv,'UV'),(progress,'DepositProgress')]:
        if not EDIT.connect_material_expressions(source,'',mask,name):raise RuntimeError('Cannot connect sealant input')
    output(mask,'',unreal.MaterialProperty.MP_OPACITY_MASK)
    EDIT.recompile_material(material)
    ASSETS.set_metadata_tag(material,'DarkFactorySealantSHA256',signature)
    if not ASSETS.save_loaded_asset(material,only_if_is_dirty=False):raise RuntimeError('Cannot save sealant material')
    return {'material':PATH,'status':'generated','sha256':signature}

if __name__=='__main__':
    result=generate();replay=generate();assert replay['status']=='unchanged'
    (ROOT/'artifacts').mkdir(exist_ok=True)
    (ROOT/'artifacts/panel-sealant-material.json').write_text(json.dumps({'passed':True,'result':result,'replay_idempotent':True},indent=2)+'\n')
    unreal.log('DF_PANEL_SEALANT_CONTENT_READY '+result['status'])
