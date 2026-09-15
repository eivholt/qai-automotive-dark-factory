"""Split locally purchased panel-rack/fence islands without dropping geometry.

Run after prepare_welding_cell.py. Only derived files change. The delta FBX is
exported by fbx_prepare group-subset and imported through import_welding_cell.ps1.
The source split is exact; runtime assembly motions remain illustrative.
"""
import argparse
from collections import Counter
import json
from pathlib import Path
import numpy as np
from prepare_welding_cell import PREFIX, write_json


def prepare(root: Path):
    scan = json.loads((root / 'scan/islands.json').read_text())
    report = json.loads((root / 'grouping_report.json').read_text())
    scene = json.loads((root / 'scene.json').read_text())
    manifest = json.loads((root / 'manifest.json').read_text())
    islands = {(m['name'], a['id']): a for m in scan['meshes'] for a in m['islands']}
    groups = {g['id']: g for g in report['groups']}
    owners = {(i['mesh'], i['island']): g['id'] for g in groups.values() for i in g['islands']}
    if set(owners) != set(islands) or sum(len(g['islands']) for g in groups.values()) != len(owners):
        raise ValueError('Input assignments do not exactly conserve source islands')
    # The first 1,228 black islands are the complete mesh grid, frame and post
    # caps. Remaining black islands belong to the control cabinet and stay.
    selections = [('SM_AssemblyFence_Black', 'robots_plastic_black', range(1228), 'assembly_fence'),
                  ('SM_AssemblyFence_Yellow', 'robots_yellow', list(range(1,21))+list(range(30,70)), 'assembly_fence'),
                  ('SM_PanelSupply_Rack', 'robots_blue', list(range(10))+list(range(158,166)), 'panel_supply_rack'),
                  ('SM_AssemblyPedestal_R1', 'robots_tiles', [10], 'assembly_pedestal'),
                  ('SM_AssemblyPedestal_R2', 'robots_tiles', [8], 'assembly_pedestal'),
                  ('SM_AssemblyPedestal_R4', 'robots_tiles', [7], 'assembly_pedestal'),
                  ('SM_AssemblyFloorFixture_R1', 'chrome', range(56,251), 'assembly_floor_fixture'),
                  ('SM_AssemblyFloorFixture_R2', 'chrome', list(range(251,347))+list(range(449,548)), 'assembly_floor_fixture'),
                  ('SM_AssemblyFloorDeck_R1', 'robots_tiles', [2], 'assembly_floor_deck'),
                  ('SM_AssemblyFloorDeck_R2', 'robots_tiles', [1], 'assembly_floor_deck')]
    selections += [(f'SM_PanelSupply_{i}', 'chrome', [551+i], 'panel_supply') for i in range(5)]
    affected = set()
    for name, suffix, numbers, role in selections:
        selected = {(PREFIX+suffix, n) for n in numbers}
        if not selected <= set(islands):
            raise ValueError('Pinned purchased source islands missing: '+name)
        for key in selected:
            old = owners[key]
            if old != name and not old.startswith('SM_Background_'):
                raise ValueError('Selection overlaps a body/robot component')
            if old != name:
                groups[old]['islands'] = [a for a in groups[old]['islands'] if (a['mesh'],a['island']) != key]
                affected.add(old)
            owners[key] = name
        low=np.min([islands[k]['min'] for k in selected],axis=0)
        high=np.max([islands[k]['max'] for k in selected],axis=0)
        if role=='panel_supply' and any(islands[k]['triangles'] != 13920 for k in selected):
            raise ValueError('Expected exact 13,920-triangle inner trunk pressing')
        groups[name]={'id':name,'role':role,'parent_joint':'','pivot_cm':((low+high)/2).tolist(),
                      'triangles':sum(islands[k]['triangles'] for k in selected),
                      'islands':[{'mesh':m,'island':n} for m,n in sorted(selected)]}
        affected.add(name)
    components={c['id']:c for c in scene['components']}
    mesh_rows={m['id']:m for m in manifest['meshes']}
    for name,g in groups.items():
        if not g['islands']:
            raise ValueError('Unexpected empty background group '+name)
        g['triangles']=sum(islands[a['mesh'],a['island']]['triangles'] for a in g['islands'])
        if name not in affected: continue
        low=np.min([islands[a['mesh'],a['island']]['min'] for a in g['islands']],axis=0)
        high=np.max([islands[a['mesh'],a['island']]['max'] for a in g['islands']],axis=0)
        pivot=np.array(g['pivot_cm'])
        mesh_rows[name]={'id':name,'fbx_node':name,'expected_bounds_min_cm':(low-pivot).tolist(),
                         'expected_bounds_max_cm':(high-pivot).tolist()}
        if g['role'].startswith(('assembly_','panel_supply')):
            components[name]={'id':name,'asset_path':f'/Game/WeldingCell/Meshes/{name}.{name}',
                 'parent_joint':'','location_cm':pivot.tolist(),'rotation_deg':[0,0,0],
                 'scale_xyz':[1,1,1],'role':g['role']}
            # Deliberately no new profile here: the process binds equal-temperature
            # supply pieces to one explicit shared imported-surface profile.
    total=sum(m['triangles'] for m in scan['meshes'])
    if total != sum(g['triangles'] for g in groups.values()) or len(owners)!=sum(len(g['islands']) for g in groups.values()):
        raise AssertionError('Triangle or island conservation failed')
    scene['components']=[components[k] for k in sorted(components)]
    report.update(groups=[groups[k] for k in sorted(groups)],components=len(groups),assigned_triangles=total,
                  assigned_islands=len(owners),geometry_conserved=True,
                  roles=dict(sorted(Counter(g['role'] for g in groups.values()).items())))
    manifest['meshes']=[mesh_rows[k] for k in sorted(mesh_rows)]
    with (root/'assignments.tsv').open('w',encoding='utf-8',newline='\n') as f:
        f.write('# source_mesh island_id export_group pivot_x_cm pivot_y_cm pivot_z_cm\n')
        for (mesh,i),name in sorted(owners.items()):
            v=groups[name]['pivot_cm']
            f.write(f'{mesh}\t{i}\t{name}\t{v[0]:.9g}\t{v[1]:.9g}\t{v[2]:.9g}\n')
    # Always include all five residual backgrounds on replay so the export is
    # reproducible even after ownership already moved in a preceding run.
    affected.update('SM_Background_'+s for s in ['chrome','robots_blue','robots_plastic_black','robots_yellow','robots_tiles'])
    for name,data in [('scene.json',scene),('manifest.json',manifest),('grouping_report.json',report),
        ('panel_delta_manifest.json',{'version':1,'fbx':'panel_delta.fbx','meshes':[mesh_rows[k] for k in sorted(affected)]})]:
        write_json(root/name,data)
    (root/'panel_delta_groups.txt').write_text('\n'.join(sorted(affected))+'\n',encoding='utf-8')
    result={'schema':'panel-source-regroup-v1','geometry_conserved':True,'source_triangles':total,
            'source_islands':len(islands),'assigned_triangles':total,'assigned_islands':len(owners),
            'body_template':'SM_BodyPanel_0386','supply_source_islands':list(range(551,556)),
            'fence_black_islands':[0,1227],'fence_yellow_islands':list(range(1,21))+list(range(30,70)),
            'floor_fixture_chrome_islands':{'R1':list(range(56,251)),'R2':list(range(251,347))+list(range(449,548))},
            'floor_fixture_tile_islands':{'R1':2,'R2':1},
            'pedestal_tile_islands':{'R1':10,'R2':8,'R4':7},
            'changed_meshes':sorted(affected),'total_meshes':len(groups)}
    write_json(root/'panel_grouping_report.json',result)
    return result

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--root',type=Path,default=Path('assets/derived/welding_cell'))
    print(json.dumps(prepare(parser.parse_args().root),indent=2))
