"""Copy selected user-supplied Isaac textures, retaining their manifest provenance."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parents[2]

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT.parent / 'qai-physics-reasoning/unreal_conveyor_demo/Content/OmniversePayload')
    args = parser.parse_args()
    base = args.source / 'omniverse-content-production.s3-us-west-2.amazonaws.com/Assets/Isaac/6.0/NVIDIA/Assets/DigitalTwin/Materials/Base'
    destination = ROOT / 'assets/derived/industrial_materials'
    destination.mkdir(parents=True, exist_ok=True)
    records = []
    for category, name in [('Rubber', 'Rubber_Black_Rough_A'), ('Metals', 'Metal_Rough_A')]:
        for suffix in ('Albedo', 'Normal', 'ORM'):
            source = base / category / 'Textures' / f'T_{name}_{suffix}.png'
            target = destination / source.name
            shutil.copy2(source, target)
            records.append(dict(file=target.name, source=str(source), sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
        shutil.copy2(base / category / f'{name}.mdl', destination / f'{name}.mdl')
    (destination / 'provenance.json').write_text(json.dumps({'provider':'NVIDIA Isaac 6.0 DigitalTwin materials; locally supplied by user', 'redistribute':False, 'files':records}, indent=2) + '\n')
    print(destination)

if __name__ == '__main__':
    main()
