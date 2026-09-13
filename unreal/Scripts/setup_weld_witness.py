"""Idempotently add cosmetic weld contact marks while preserving source PBR/heat branches."""
from pathlib import Path
import hashlib,json,runpy
import unreal
ROOT=Path(__file__).resolve().parents[2]
H=runpy.run_path(str(Path(__file__).with_name('setup_welding_tool_materials.py')))
EDIT=unreal.MaterialEditingLibrary
ASSETS=unreal.EditorAssetLibrary
PREFIX='DF_WELD_WITNESS_V1/'
CODE=(ROOT/'unreal/Shaders/WeldWitness.usf').read_text()
SCALARS={'WeldWitnessEnabled':0.,**{f'WitnessStrength{i}':0. for i in range(4)}}
VECTORS={k:v for i in range(4) for k,v in [(f'WitnessCenter{i}',(0.,0.,0.,0.)),(f'WitnessNormal{i}',(0.,0.,1.,0.))]}
def augment(material):
    nodes=H['owned_nodes'](material,PREFIX)
    if nodes:
        changed=nodes['hotspot'].get_editor_property('code')!=CODE
        if changed: nodes['hotspot'].set_editor_property('code',CODE)
        H['audit_custom'](material,nodes['hotspot'],nodes,SCALARS,VECTORS,CODE,nodes['world_position'])
        for key,prop in [('color_result',unreal.MaterialProperty.MP_BASE_COLOR),('rough_result',unreal.MaterialProperty.MP_ROUGHNESS)]:
            if EDIT.get_material_property_input_node(material,prop)!=nodes[key]:
                raise RuntimeError('Disconnected witness branch '+material.get_path_name())
        if changed:
            EDIT.recompile_material(material)
            ASSETS.set_metadata_tag(material,'DarkFactoryWeldWitnessSHA256',hashlib.sha256(CODE.encode()).hexdigest())
            if not ASSETS.save_loaded_asset(material,only_if_is_dirty=False):raise RuntimeError('Cannot save updated witness shader')
        return {'material':material.get_path_name(),'status':'updated' if changed else 'unchanged'}
    color_prop=unreal.MaterialProperty.MP_BASE_COLOR
    rough_prop=unreal.MaterialProperty.MP_ROUGHNESS
    inputs={p:(EDIT.get_material_property_input_node(material,p),str(EDIT.get_material_property_input_node_output_name(material,p))) for p in [color_prop,rough_prop]}
    if any(node is None for node,_ in inputs.values()):
        raise RuntimeError('Expected connected source color/roughness before witness augmentation')
    make=lambda cls,role,x,y:H['node'](material,cls,role,x,y,PREFIX)
    link=H['connect']
    world=make(unreal.MaterialExpressionWorldPosition,'world_position',-1000,2800)
    mask=H['custom_block'](material,SCALARS,VECTORS,CODE,world,PREFIX,2800,'Cosmetic weld witness')
    mask.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    # Lerp existing source to a darkened copy; the untouched source is input A.
    dark=make(unreal.MaterialExpressionMultiply,'dark_source',450,2700)
    dark.set_editor_property('const_b',0.32)
    link(*inputs[color_prop],dark,'A')
    color=make(unreal.MaterialExpressionLinearInterpolate,'color_result',700,2700)
    link(*inputs[color_prop],color,'A');link(dark,'',color,'B');link(mask,'',color,'Alpha')
    rough=make(unreal.MaterialExpressionLinearInterpolate,'rough_result',700,2950)
    rough.set_editor_property('const_b',0.72)
    link(*inputs[rough_prop],rough,'A');link(mask,'',rough,'Alpha')
    if not EDIT.connect_material_property(color,'',color_prop) or not EDIT.connect_material_property(rough,'',rough_prop):
        raise RuntimeError('Cannot connect witness surface outputs')
    EDIT.recompile_material(material)
    ASSETS.set_metadata_tag(material,'DarkFactoryWeldWitnessSHA256',hashlib.sha256(CODE.encode()).hexdigest())
    if not ASSETS.save_loaded_asset(material,only_if_is_dirty=False):raise RuntimeError('Cannot save witness material')
    return {'material':material.get_path_name(),'status':'augmented'}
def augment_materials(materials=None):
    if materials is None:
        materials=[ASSETS.load_asset(p) for p in ASSETS.list_assets('/Game/WeldingCell/Materials',False,False) if p.rsplit('/',1)[-1].startswith('M_WC_')]
    return [augment(m) for m in materials]
if __name__=='__main__':
    rows=augment_materials(); replay=augment_materials()
    assert all(row['status']=='unchanged' for row in replay)
    report={'passed':True,'materials':rows,'replay_idempotent':True,'shader_sha256':hashlib.sha256(CODE.encode()).hexdigest()}
    (ROOT/'artifacts').mkdir(exist_ok=True)
    (ROOT/'artifacts/weld-witness-materials.json').write_text(json.dumps(report,indent=2)+'\n')
    unreal.log('DF_WELD_WITNESS_CONTENT_READY materials='+str(len(rows)))
