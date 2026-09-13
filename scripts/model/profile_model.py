#!/usr/bin/env python3
"""Bounded stock Torch/PEFT diagnostics and one CPU/CUDA operator profile."""
import argparse
from collections import Counter
import json
import os
from pathlib import Path
import sys
import time
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"src"))
from dark_factory.gates import load_gate_contract, camera_content, select_decision, validate_adapter_training, MESSAGE_LAYOUT
from train_lora import load_rows


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--base",required=True)
    p.add_argument("--adapter",required=True)
    p.add_argument("--manifest",required=True,help="Prepared train.jsonl or validation.jsonl only")
    p.add_argument("--sample-ids",nargs="+",required=True)
    p.add_argument("--output",required=True)
    args=p.parse_args()
    if Path(args.manifest).name not in ("train.jsonl","validation.jsonl") or len(args.sample_ids)>8:
        p.error("Use a prepared training/validation manifest and at most eight samples; test profiling is forbidden")
    output=Path(args.output)
    if output.exists() and any(output.iterdir()): p.error("Choose a new private output directory")
    contract=load_gate_contract()
    validate_adapter_training(json.loads((Path(args.adapter)/"training_report.json").read_text(encoding="utf-8")),contract)
    available={r["sample_id"]:r for r in load_rows(args.manifest,contract)}
    rows=[available[k] for k in args.sample_ids]
    import torch
    from PIL import Image
    from transformers import AutoProcessor,Qwen3VLForConditionalGeneration
    from peft import PeftModel
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    processor=AutoProcessor.from_pretrained(args.base,local_files_only=True)
    processor.image_processor.size={"shortest_edge":448*256,"longest_edge":448*256}
    base=Qwen3VLForConditionalGeneration.from_pretrained(args.base,local_files_only=True,dtype=torch.bfloat16,attn_implementation="sdpa").to("cuda").eval()
    model=PeftModel.from_pretrained(base,args.adapter,local_files_only=True).eval()
    ids={k:processor.tokenizer.encode(k,add_special_tokens=False)[0] for k in "GAR"}
    prepared=[]
    for row in rows:
        images=[Image.open(path).convert("RGB") for path in row["images"]]
        messages=[{"role":"user","content":camera_content([{"type":"image","image":im} for im in images],row["prompt"])}]
        inputs=processor.apply_chat_template(messages,tokenize=True,add_generation_prompt=True,return_dict=True,return_tensors="pt").to("cuda")
        if any(g!=[1,16,28] for g in inputs["image_grid_thw"].cpu().tolist()): raise ValueError("Unexpected camera token grid")
        prepared.append(inputs)
    def forward(inputs):
        with torch.inference_mode(),torch.autocast(device_type="cuda",dtype=torch.bfloat16):
            return model(**inputs,use_cache=False,logits_to_keep=1).logits[0,-1].float()
    forward(prepared[0]); torch.cuda.synchronize()
    records=[]
    for row,inputs in zip(rows,prepared):
        started=time.perf_counter(); logits=forward(inputs); torch.cuda.synchronize()
        label_logits=logits[[ids[k] for k in "GAR"]]
        mass=float((torch.logsumexp(label_logits,0)-torch.logsumexp(logits,0)).exp())
        scores=dict(zip("GAR",label_logits.softmax(0).cpu().tolist()))
        decision,reason=select_decision(scores,min(mass,1),contract["decision_policy"])
        records.append({"sample_id":row["sample_id"],"stage":row["stage"],"expected":row["answer"],"decision":decision,"reason":reason,"scores":scores,"label_mass":mass,"forward_ms":(time.perf_counter()-started)*1000})
    output.mkdir(parents=True,exist_ok=True)
    # Save useful predictions before optional instrumentation, which may fail.
    (output/"normal_forward_records.json").write_text(json.dumps(records,indent=2),encoding="utf-8")
    timings=[]
    pending={}
    handles=[]
    def before(name):
        def hook(module, inputs):
            event=torch.cuda.Event(enable_timing=True); event.record()
            pending[name]=(event,time.perf_counter())
        return hook
    def after(name):
        def hook(module, inputs, result):
            event=torch.cuda.Event(enable_timing=True); event.record()
            start,cpu_start=pending.pop(name)
            timings.append((name,type(module).__name__,start,event,(time.perf_counter()-cpu_start)*1e6))
        return hook
    for name,module in model.named_modules():
        if not list(module.children()):
            handles.extend([module.register_forward_pre_hook(before(name)),module.register_forward_hook(after(name))])
    try:
        forward(prepared[0]); torch.cuda.synchronize()
    finally:
        for handle in handles: handle.remove()
    operators=[{"name":name,"module_type":kind,"calls":1,"self_cpu_us":cpu_us,
        "self_cuda_us":start.elapsed_time(end)*1000} for name,kind,start,end,cpu_us in timings]
    report={"message_layout":MESSAGE_LAYOUT,"scoring_method":"gar-full-vocabulary-logits-v1","scope":"Stock Torch diagnostic with leaf-module CUDA event timing; CUPTI operator profiler exceeded its time bound and was terminated. Module timing is not a hardware benchmark", "records":records,
        "parameter_placement":dict(Counter(str(p.device)+"/"+str(p.dtype) for p in model.parameters())),
        "parameter_elements":dict(Counter({str(d):sum(p.numel() for p in model.parameters() if p.device.type==d) for d in {p.device.type for p in model.parameters()}})),
        "inputs":[{k:{"shape":list(v.shape),"dtype":str(v.dtype),"device":str(v.device)} for k,v in inputs.items()} for inputs in prepared],
        "torch":torch.__version__,"cuda":torch.version.cuda,"cudnn":torch.backends.cudnn.version(),"gpu":torch.cuda.get_device_name(),
        "environment":{k:os.environ.get(k) for k in ("CUDA_LAUNCH_BLOCKING","CUDA_VISIBLE_DEVICES","PYTORCH_CUDA_ALLOC_CONF")},
        "backend_flags":{"cudnn_enabled":torch.backends.cudnn.enabled,"cudnn_benchmark":torch.backends.cudnn.benchmark,"cudnn_allow_tf32":torch.backends.cudnn.allow_tf32,"matmul_allow_tf32":torch.backends.cuda.matmul.allow_tf32},
        "peak_cuda_allocated_bytes":torch.cuda.max_memory_allocated(),"operators":sorted(operators,key=lambda e:e["self_cuda_us"],reverse=True)}
    (output/"report.json").write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({"records":records,"top_cuda_operators":report["operators"][:12]},indent=2),flush=True)

if __name__=="__main__": main()
