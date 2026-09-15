"""Loopback-only experimental Cosmos Reason2 BF16 next-token classifier.

Optional host dependencies are imported only when starting this server. Existing
EVK and parcel services are neither contacted nor modified.
"""
from __future__ import annotations
import argparse
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
from importlib.metadata import version as package_version
import json
from pathlib import Path
import threading
import time
from .gates import MAX_GATE_REQUEST_BYTES, LABELS, load_gate_contract, parse_gate_request, hold_reply, select_decision, camera_content, MESSAGE_LAYOUT, validate_adapter_training, model_export_alias
from .protocol import ProtocolError
from .gates_scores import topk_label_bounds, decision_from_bounds, SCORING_METHOD


class Reason2Classifier:
    def __init__(self, model_path: str, contract: dict, adapter: str | None = None, *, linear_patch_embedding: bool = False, allowed_stages=None):
        self.model_config_sha256 = hashlib.sha256((Path(model_path) / "config.json").read_bytes()).hexdigest()
        self.allowed_stages = tuple(allowed_stages or contract["stages"])
        training_report = None
        if adapter:
            training_report = json.loads((Path(adapter) / "training_report.json").read_text(encoding="utf-8"))
            validate_adapter_training(training_report, contract, self.model_config_sha256, self.allowed_stages)
        import torch
        import transformers
        from transformers import AutoProcessor, Qwen3VLForConditionalGeneration
        if not torch.cuda.is_available():
            raise RuntimeError("A CUDA host GPU is required; no silent CPU/mock fallback")
        torch.set_num_threads(4)
        if torch.get_num_interop_threads() != 1:
            torch.set_num_interop_threads(1)
        self.torch, self.contract = torch, contract
        self.scoring_method = "gar-full-vocabulary-logits-v1"
        self.gpu_name = torch.cuda.get_device_name()
        self.processor = AutoProcessor.from_pretrained(model_path, local_files_only=True)
        # Qwen3-VL uses 32x32 pixels per merged token. No image resize at 448x256.
        self.processor.image_processor.size = {"shortest_edge": 448 * 256, "longest_edge": 448 * 256}
        self.model = Qwen3VLForConditionalGeneration.from_pretrained(
            model_path, local_files_only=True, dtype=torch.bfloat16,
            attn_implementation="sdpa").to("cuda").eval()
        if adapter:
            from peft import PeftModel
            self.model = PeftModel.from_pretrained(self.model, adapter, local_files_only=True).merge_and_unload().eval()
        self.vision_patch_embedding = "linear-nonoverlap-v1" if linear_patch_embedding else "stock-conv3d"
        self.patch_context = None
        if linear_patch_embedding:
            from .gates_patch_embed import linear_qwen3_vl_patch_embed
            self.patch_context = linear_qwen3_vl_patch_embed(self.model, unsupported="raise")
            if self.patch_context.__enter__() != 1:
                raise RuntimeError("Expected exactly one supported vision patch projection")
        def sha_file(path):
            digest = hashlib.sha256()
            with Path(path).open("rb") as stream:
                for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
                    digest.update(block)
            return digest.hexdigest()
        base_sha = sha_file(Path(model_path) / "model.safetensors")
        adapter_sha = sha_file(Path(adapter) / "adapter_model.safetensors") if adapter else None
        self.provenance = {"model": "nvidia/Cosmos-Reason2-2B", "runtime": "torch-bf16-merged-adapter-v1",
            "vision_patch_embedding": self.vision_patch_embedding, "base_weights_sha256": base_sha,
            "adapter_weights_sha256": adapter_sha, "base_model_config_sha256": self.model_config_sha256,
            "adapter_config_sha256": sha_file(Path(adapter) / "adapter_config.json") if adapter else None,
            "torch_version": str(torch.__version__), "transformers_version": transformers.__version__, "peft_version": package_version("peft") if adapter else None,
            "processor_files_sha256": {p.name: sha_file(p) for p in sorted(Path(model_path).glob("*")) if p.is_file() and (p.suffix in (".json", ".jinja") or p.name == "vocab.txt")},
            "projection_source_sha256": sha_file(Path(__file__).with_name("gates_patch_embed.py")) if linear_patch_embedding else None,
            "message_layout": MESSAGE_LAYOUT, "contract_version": contract["version"],
            "prompt_hashes": {s:contract["stages"][s]["prompt_sha256"] for s in self.allowed_stages},
            "allowed_stages": list(self.allowed_stages),
            "adapter_training_report_sha256": sha_file(Path(adapter) / "training_report.json") if adapter else None,
            "training_projection": training_report.get("vision_patch_embedding", "stock-conv3d") if training_report else None}
        identity_sha = hashlib.sha256(json.dumps(self.provenance, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        self.model_alias = f"Cosmos-Reason2-2B-Torch-BF16-{identity_sha[:32]}-{self.vision_patch_embedding}"
        self.model_name = "nvidia/Cosmos-Reason2-2B/" + ("adapter-bf16" if adapter else "base-bf16")
        self.adapter = str(Path(adapter).resolve()) if adapter else None
        self.token_ids = []
        for label in LABELS:
            ids = self.processor.tokenizer.encode(label, add_special_tokens=False)
            if len(ids) != 1:
                raise RuntimeError(f"Class label {label} must be exactly one token")
            self.token_ids.append(ids[0])
        self.lock = threading.Lock()
        self.warmed = False
        self.model_path = str(Path(model_path).resolve())
        self.model_config_sha256 = hashlib.sha256((Path(model_path) / "config.json").read_bytes()).hexdigest()

    def inspect(self, request):
        if request.identity["stage"] not in self.allowed_stages:
            raise ProtocolError("Stage is outside this adapter runtime scope")
        from PIL import Image, ImageChops
        torch = self.torch
        started = time.perf_counter()
        images = []
        for png in request.pngs:
            with Image.open(io.BytesIO(png)) as source:
                if "A" in source.getbands() and source.getchannel("A").getextrema() != (255, 255):
                    raise ProtocolError("Sensor PNG alpha must be fully opaque")
                rgb = source.convert("RGB")
                r, g, b = rgb.split()
                if ImageChops.difference(r, g).getbbox() or ImageChops.difference(r, b).getbbox():
                    raise ProtocolError("Process model input must be grayscale; observer palettes are rejected")
                images.append(rgb)
        prompt = self.contract["stages"][request.identity["stage"]]["prompt"]
        messages = [{"role": "user", "content": camera_content([{"type": "image", "image": im} for im in images], prompt)}]
        inputs = self.processor.apply_chat_template(messages, tokenize=True, add_generation_prompt=True,
            return_dict=True, return_tensors="pt").to("cuda")
        grids = inputs["image_grid_thw"].detach().cpu().tolist()
        if any(grid != [1, 16, 28] for grid in grids):
            raise RuntimeError(f"Processor changed camera image geometry: {grids}")
        torch.cuda.synchronize()
        preprocessing_ms = (time.perf_counter() - started) * 1000
        inference_started = time.perf_counter()
        with torch.inference_mode():
            # No autoregressive decode, KV reuse, hidden cache, JSON generation or CoT.
            logits = self.model(**inputs, use_cache=False, logits_to_keep=1).logits[0, -1].float()
            labels = logits[self.token_ids]
            log_mass = torch.logsumexp(labels, dim=0) - torch.logsumexp(logits, dim=0)
            scores = labels.softmax(dim=0).cpu().tolist()
            mass = log_mass.exp().item()
            raw_top = int(logits.argmax().item())
        torch.cuda.synchronize()
        inference_ms = (time.perf_counter() - inference_started) * 1000
        conditional = dict(zip(LABELS, scores))
        decision, reason = select_decision(conditional, mass, self.contract["decision_policy"])
        total_ms = (time.perf_counter() - started) * 1000
        result = hold_reply(request.identity, self.contract, reason, valid=True)
        result.update(decision=decision, model=self.model_name, latency_ms=round(total_ms, 3),
            adapter=self.adapter, backend_alias=self.model_alias, vision_patch_embedding=self.vision_patch_embedding,
            scoring_method=self.scoring_method, conditional_label_scores=conditional, label_mass=mass,
            raw_top_token=self.processor.tokenizer.decode([raw_top]),
            image_sha256=[hashlib.sha256(png).hexdigest() for png in request.pngs],
            prompt_tokens=int(inputs["input_ids"].shape[1]), visual_tokens=112 * len(images),
            timing={"preprocessing_ms": round(preprocessing_ms, 3), "inference_ms": round(inference_ms, 3), "total_ms": round(total_ms, 3)})
        return result

    def close(self):
        if self.patch_context is not None:
            self.patch_context.__exit__(None, None, None)
            self.patch_context = None

    def is_ready(self):
        return self.warmed

    def warmup(self):
        from PIL import Image
        import base64
        buffer = io.BytesIO()
        Image.new("L", (448, 256), 30).save(buffer, format="PNG")
        png = base64.b64encode(buffer.getvalue()).decode()
        for stage in self.allowed_stages:
            spec = self.contract["stages"][stage]
            payload = {"contract_version": self.contract["version"], "request_id": "warmup-" + stage,
                "body_id": "warmup", "stage": stage, "captured_at_ms": 0,
                "images": [{"camera_id": camera, "png_base64": png} for camera in spec["camera_ids"]]}
            self.inspect(parse_gate_request(payload, self.contract))
        self.warmed = True


class ModalityClassifier:
    """Two explicit resident models; routing depends only on the requested stage."""
    def __init__(self, model_path, contract, primary_adapter, secondary_adapter, *, linear_patch_embedding=False, weld_only=False):
        self.contract = contract
        primary_key, secondary_key = ("primary", "weld") if weld_only else ("nir", "thermal")
        self.routing = "resident-merged-weld-override-v1" if weld_only else "resident-merged-modality-routing-v1"
        assignment = {stage: secondary_key if (stage == "weld" if weld_only else spec["modality"] == "thermal") else primary_key
            for stage, spec in contract["stages"].items()}
        if weld_only and "weld" not in contract["stages"]:
            raise ValueError("Weld override requires the weld stage")
        self.models = {}
        try:
            for key, adapter in ((primary_key, primary_adapter), (secondary_key, secondary_adapter)):
                self.models[key] = Reason2Classifier(model_path, contract, adapter, linear_patch_embedding=linear_patch_embedding,
                    allowed_stages=[stage for stage, owner in assignment.items() if owner == key])
        except Exception:
            for model in self.models.values():
                model.close()
            raise
        self.stage_models = {stage: self.models[key] for stage, key in assignment.items()}
        self.stage_backend_aliases = {stage: model.model_alias for stage, model in self.stage_models.items()}
        self.provenance = {"runtime": self.routing, "stage_backend_aliases": self.stage_backend_aliases,
            "models": {key: model.provenance for key, model in self.models.items()}}
        identity = hashlib.sha256(json.dumps(self.provenance, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
        self.model_alias = ("Cosmos-Reason2-2B-Stage-Router-" if weld_only else "Cosmos-Reason2-2B-Modality-Router-") + identity[:32]
        self.model_name = "nvidia/Cosmos-Reason2-2B/" + ("stage-adapters-bf16" if weld_only else "modality-adapters-bf16")
        self.adapter = {key: model.adapter for key, model in self.models.items()}
        primary = self.models[primary_key]
        self.gpu_name = primary.gpu_name
        self.model_config_sha256 = primary.model_config_sha256
        self.scoring_method = "gar-full-vocabulary-logits-v1"
        self.vision_patch_embedding = primary.vision_patch_embedding
        self.lock = threading.Lock()
        self.warmed = False

    def inspect(self, request):
        stage = request.identity["stage"]
        selected = self.stage_models[stage]
        reply = selected.inspect(request)
        reply.update(model=self.model_name, backend_alias=self.model_alias,
            stage_backend_alias=selected.model_alias, routing=self.routing)
        return reply

    def warmup(self):
        for model in self.models.values():
            model.warmup()
        self.warmed = True

    def is_ready(self):
        return self.warmed and all(model.is_ready() for model in self.models.values())

    def close(self):
        for model in self.models.values():
            model.close()


class LlamaClassifier:
    """Same image contract, served by a separate local llama.cpp CUDA process."""
    def __init__(self, endpoint: str, contract: dict, provenance_path: str):
        import urllib.request
        if endpoint not in ("http://127.0.0.1:18192/v1/chat/completions", "http://127.0.0.1:18194/v1/chat/completions"):
            raise ValueError("Experimental backend must use a dedicated loopback endpoint")
        self.endpoint, self.contract = endpoint, contract
        self.scoring_method = SCORING_METHOD
        self.vision_patch_embedding = "llamacpp-native"
        self.lock = threading.Lock()
        self.model_name = "nvidia/Cosmos-Reason2-2B/base-bf16-llamacpp"
        self.adapter = None
        self.provenance = json.loads(Path(provenance_path).read_text(encoding="utf-8"))
        if self.provenance.get("model") != "nvidia/Cosmos-Reason2-2B":
            raise ValueError("Expected provenance of a Reason2 model")
        self.adapter = self.provenance.get("adapter")
        if self.adapter:
            ap = self.provenance.get("adapter_provenance", {})
            validate_adapter_training(ap.get("training_report"), contract, required_stages=contract["stages"])
            if ap.get("message_layout") != MESSAGE_LAYOUT:
                raise ValueError("Automotive adapter message layout does not match the runtime")
            if ap.get("contract_version") != contract["version"] or ap.get("prompt_hashes") != {k:v["prompt_sha256"] for k,v in contract["stages"].items()}:
                raise ValueError("Automotive adapter training prompts do not match the process contract")
            self.model_name = "nvidia/Cosmos-Reason2-2B/automotive-adapter-bf16-llamacpp"
        self.model_alias = model_export_alias(self.provenance)
        self.model_config_sha256 = self.provenance["files"]["model_config"]["sha256"]
        self.warmed = False
        self.gpu_name = "CUDA GPU (see backend startup log)"

    def verify_backend_identity(self):
        import urllib.request
        with urllib.request.urlopen(self.endpoint.split("/v1/")[0] + "/v1/models", timeout=1) as response:
            models = json.load(response)
        if [m.get("id") for m in models.get("data", [])] != [self.model_alias]:
            raise ProtocolError("GPU backend model identity differs from export provenance")

    def inspect(self, request):
        import base64
        import math
        import urllib.request
        from PIL import Image, ImageChops
        started = time.perf_counter()
        self.verify_backend_identity()
        for png in request.pngs:
            with Image.open(io.BytesIO(png)) as source:
                if "A" in source.getbands() and source.getchannel("A").getextrema() != (255, 255):
                    raise ProtocolError("Sensor PNG alpha must be fully opaque")
                r, g, b = source.convert("RGB").split()
                if ImageChops.difference(r, g).getbbox() or ImageChops.difference(r, b).getbbox():
                    raise ProtocolError("Process model input must be grayscale")
        content = [{"type": "image_url", "image_url": {"url": "data:image/png;base64," + base64.b64encode(png).decode()}} for png in request.pngs]
        content = camera_content(content, self.contract["stages"][request.identity["stage"]]["prompt"])
        payload = {"model": self.model_alias, "messages": [{"role": "user", "content": content}],
            "max_tokens": 1, "temperature": 0, "top_k": 1, "seed": 42, "stream": False,
            "logprobs": True, "top_logprobs": 20, "cache_prompt": False}
        preprocessing_ms = (time.perf_counter() - started) * 1000
        inference_start = time.perf_counter()
        transport = urllib.request.Request(self.endpoint, json.dumps(payload).encode(), {"Content-Type": "application/json"})
        with urllib.request.urlopen(transport, timeout=25) as response:
            raw = json.load(response)
        inference_ms = (time.perf_counter() - inference_start) * 1000
        if raw.get("model") != self.model_alias:
            raise ProtocolError("GPU response model identity differs from export provenance")
        choice = raw["choices"][0]
        candidates = choice["logprobs"]["content"][0]["top_logprobs"]
        bounds = topk_label_bounds(candidates)
        raw_top = choice["message"]["content"]
        decision, reason = decision_from_bounds(bounds, self.contract["decision_policy"])
        total_ms = (time.perf_counter() - started) * 1000
        result = hold_reply(request.identity, self.contract, reason, valid=True)
        result.update(decision=decision, model=self.model_name, backend_alias=self.model_alias, vision_patch_embedding=self.vision_patch_embedding, latency_ms=round(total_ms, 3), adapter=self.adapter,
            conditional_label_scores=None, label_mass=bounds["label_mass_lower_bound"], raw_top_token=raw_top, **bounds,
            image_sha256=[hashlib.sha256(png).hexdigest() for png in request.pngs],
            prompt_tokens=raw.get("usage", {}).get("prompt_tokens"), visual_tokens=112 * len(request.pngs),
            timing={"preprocessing_ms": round(preprocessing_ms, 3), "inference_ms": round(inference_ms, 3), "total_ms": round(total_ms, 3)},
            backend_timing=raw.get("timings", {}))
        return result

    def is_ready(self):
        if not self.warmed:
            return False
        import urllib.error
        import urllib.request
        try:
            with urllib.request.urlopen(self.endpoint.split("/v1/")[0] + "/health", timeout=1) as response:
                return json.load(response).get("status") == "ok"
        except (OSError, ValueError, urllib.error.URLError):
            return False

    warmup = Reason2Classifier.warmup


def make_handler(classifier):
    class Handler(BaseHTTPRequestHandler):
        def setup(self):
            super().setup()
            self.connection.settimeout(15)

        def send_json(self, status, payload):
            body = json.dumps(payload, allow_nan=False).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path != "/health":
                return self.send_json(404, hold_reply({}, classifier.contract, "unknown_endpoint"))
            ready = classifier.is_ready()
            self.send_json(200 if ready else 503, {"status": "ready" if ready else "unavailable", "source": "host_gpu",
                "model": classifier.model_name, "adapter": classifier.adapter, "backend_alias": getattr(classifier, "model_alias", None), "scoring_method": classifier.scoring_method,
                "contract_version": classifier.contract["version"], "stages": classifier.contract["stages"],
                "decision_policy": classifier.contract["decision_policy"], "message_layout": MESSAGE_LAYOUT,
                "gpu": classifier.gpu_name, "dtype": "bfloat16", "mock": False,
                "model_config_sha256": classifier.model_config_sha256,
                "vision_patch_embedding": getattr(classifier, "vision_patch_embedding", "llamacpp-native"),
                "stage_backend_aliases": getattr(classifier, "stage_backend_aliases", None),
                "cuda_allocated_bytes": next(iter(classifier.models.values())).torch.cuda.memory_allocated() if isinstance(classifier, ModalityClassifier) else None,
                "cuda_peak_allocated_bytes": next(iter(classifier.models.values())).torch.cuda.max_memory_allocated() if isinstance(classifier, ModalityClassifier) else None,
                "provenance": getattr(classifier, "provenance", None)})

        def do_POST(self):
            payload = {}
            locked = False
            try:
                if self.path != "/v1/inspect":
                    return self.send_json(404, hold_reply(payload, classifier.contract, "unknown_endpoint"))
                size = int(self.headers.get("Content-Length", "0"))
                if not 0 < size <= MAX_GATE_REQUEST_BYTES:
                    raise ProtocolError("Request size outside process limits")
                if self.headers.get_content_type() != "application/json":
                    raise ProtocolError("Process requests must use application/json")
                payload = json.loads(self.rfile.read(size))
                request = parse_gate_request(payload, classifier.contract)
                locked = classifier.lock.acquire(blocking=False)
                if not locked:
                    return self.send_json(429, hold_reply(payload, classifier.contract, "model_busy"))
                self.send_json(200, classifier.inspect(request))
            except (ValueError, ProtocolError) as exc:
                self.send_json(400, hold_reply(payload, classifier.contract, str(exc)))
            except Exception as exc:
                # No image payloads or scene labels are logged.
                print("PROCESS_INFERENCE_ERROR", type(exc).__name__, str(exc), flush=True)
                self.send_json(503, hold_reply(payload, classifier.contract, "model_error"))
            finally:
                if locked:
                    classifier.lock.release()

        def log_message(self, format, *args):
            print(format % args, flush=True)
    return Handler


class BoundedHTTPServer(ThreadingHTTPServer):
    """At most four header/body readers and one concurrent GPU inference."""
    def __init__(self, *args, **kwargs):
        self.readers = threading.BoundedSemaphore(4)
        super().__init__(*args, **kwargs)

    def process_request(self, request, client_address):
        self.readers.acquire()
        try:
            super().process_request(request, client_address)
        except Exception:
            self.readers.release()
            raise

    def process_request_thread(self, request, client_address):
        try:
            super().process_request_thread(request, client_address)
        finally:
            self.readers.release()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--model", help="Existing local base checkpoint; never downloads")
    p.add_argument("--llama-endpoint", help="Use dedicated llama.cpp backend on loopback18192")
    p.add_argument("--llama-provenance", default=str(Path(__file__).resolve().parents[2] / "checkpoints/process-base-bf16/provenance.json"))
    p.add_argument("--adapter", help="Optional local automotive adapter (never the parcel adapter)")
    p.add_argument("--weld-adapter", help="Optional resident adapter for weld only; requires --adapter for the other stages")
    p.add_argument("--nir-adapter", help="Explicit resident NIR model adapter; requires --thermal-adapter")
    p.add_argument("--thermal-adapter", help="Explicit resident model adapter for both thermal stages")
    p.add_argument("--contract")
    p.add_argument("--linear-patch-embedding", action="store_true", help="Opt-in Torch full-patch linear execution; exact-runtime validation required")
    p.add_argument("--port", type=int, default=18191)
    args = p.parse_args()
    routed = bool(args.nir_adapter or args.thermal_adapter)
    if args.weld_adapter and (not args.adapter or routed or args.llama_endpoint):
        p.error("Weld override requires --adapter and excludes modality/GGUF routing")
    if routed and (not args.nir_adapter or not args.thermal_adapter or args.adapter or args.llama_endpoint):
        p.error("Resident modality routing requires both modality adapters, no --adapter or GGUF endpoint")
    if args.llama_endpoint and (args.adapter or args.linear_patch_embedding):
        p.error("GGUF adapters/projection execution belong to their separately exported backend")
    if not args.llama_endpoint and not args.model:
        p.error("--model or --llama-endpoint required")
    contract = load_gate_contract(args.contract)
    if args.weld_adapter:
        classifier = ModalityClassifier(args.model, contract, args.adapter, args.weld_adapter, linear_patch_embedding=args.linear_patch_embedding, weld_only=True)
    elif routed:
        classifier = ModalityClassifier(args.model, contract, args.nir_adapter, args.thermal_adapter, linear_patch_embedding=args.linear_patch_embedding)
    elif args.llama_endpoint:
        classifier = LlamaClassifier(args.llama_endpoint, contract, args.llama_provenance)
    else:
        classifier = Reason2Classifier(args.model, contract, args.adapter, linear_patch_embedding=args.linear_patch_embedding)
    classifier.warmup()
    server = BoundedHTTPServer(("127.0.0.1", args.port), make_handler(classifier))
    server.daemon_threads = True
    server.request_queue_size = 2
    print(json.dumps({"event": "PROCESS_MODEL_READY", "port": args.port, "model": classifier.model_name,
                      "gpu": classifier.gpu_name, "mock": False, "message_layout": MESSAGE_LAYOUT}), flush=True)
    try:
        server.serve_forever(poll_interval=0.25)
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if isinstance(classifier, (Reason2Classifier, ModalityClassifier)):
            classifier.close()

if __name__ == "__main__":
    main()
