"""Offline host-GPU feasibility study; cannot serve or release the live process."""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import statistics
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'src'))
from dark_factory.gates import parse_gate_request, load_gate_contract
from dark_factory.gates_host import Reason2Classifier


def load_study(path):
    study=json.loads(Path(path).read_text())
    base=load_gate_contract()
    if study.get('version')!='automotive-sealant-coverage-study-v1' or study.get('use')!='offline_experiment_only':
        raise ValueError('Not the offline coverage contract')
    for key in ('image','model_family','generation','decision_policy'):
        if study.get(key)!=base[key]: raise ValueError(f'Unexpected {key}')
    if set(study['stages'])!={'sealant_coverage'}: raise ValueError('Unexpected stages')
    stage=study['stages']['sealant_coverage']
    if stage['camera_ids']!=['spot1','spot2','spot3'] or stage['modality']!='nir' or hashlib.sha256(stage['prompt'].encode()).hexdigest()!=stage['prompt_sha256']:
        raise ValueError('Stage contract mismatch')
    return study


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest',type=Path,required=True)
    parser.add_argument('--model',required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--contract',type=Path,default=Path(__file__).resolve().parents[2]/'configs/sealant_coverage_experiment.json')
    args=parser.parse_args()
    if args.output.exists(): raise ValueError('Use a fresh report path')
    contract=load_study(args.contract)
    rows=[json.loads(line) for line in args.manifest.read_text().splitlines() if line.strip()]
    splits={}
    for row in rows:
        episode=row['episode_id'];split=row['split']
        if episode in splits and splits[episode]!=split: raise ValueError('Episode leaks across splits')
        splits[episode]=split
        if row['contract_version']!=contract['version'] or row['stage']!='sealant_coverage' or row['prompt_sha256']!=contract['stages']['sealant_coverage']['prompt_sha256']:
            raise ValueError('Capture/prompt mismatch')
    # Development only: do not inspect held-out rows while choosing this prompt.
    rows=[r for r in rows if r['split']=='train']
    if not rows: raise ValueError('No training-development rows')
    runtime=Reason2Classifier(args.model,contract,linear_patch_embedding=True)
    results=[]
    try:
        runtime.warmup()
        for index,row in enumerate(rows):
            images=[]
            for image in row['images']:
                path=(args.manifest.parent/image['path']).resolve()
                if not path.is_relative_to(args.manifest.parent.resolve()): raise ValueError('Image outside capture directory')
                images.append({'camera_id':image['camera_id'],'png_base64':base64.b64encode(path.read_bytes()).decode()})
            request=parse_gate_request({'contract_version':contract['version'],'request_id':f'coverage-{index}',
                'body_id':row['body_id'],'stage':'sealant_coverage','captured_at_ms':index,'images':images},contract)
            result=runtime.inspect(request)
            results.append({'sample_id':row['sample_id'],'expected_provisional':row['expected_decision'],'result':result})
            print(f"{row['sample_id']}: source={row['expected_decision']} model={result['decision']} {result['latency_ms']:.0f} ms",flush=True)
    finally:
        runtime.close()
    report={'scope':'Development feasibility only; source labels require image observability review. No live gate or held-out accuracy claim.',
        'contract_version':contract['version'],'provenance':runtime.provenance,'samples':results,
        'median_latency_ms':statistics.median(r['result']['latency_ms'] for r in results),
        'source_label_agreement':sum(r['expected_provisional']==r['result']['decision'] for r in results),
        'false_accepts_against_source':sum(r['expected_provisional']!='G' and r['result']['decision']=='G' for r in results)}
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__': main()
