#!/usr/bin/env python3
"""Explicit EVK stage router for two private adapted llama.cpp backends."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import threading
from urllib.parse import urlparse

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "src"))
from dark_factory.gates import load_gate_contract
from dark_factory.gates_host import LlamaClassifier, BoundedHTTPServer, make_handler
from dark_factory.gates_scores import SCORING_METHOD
from record_edge_provenance import validate


class EdgeModel(LlamaClassifier):
    def __init__(self, endpoint, provenance_path, contract, stages):
        url = urlparse(endpoint)
        if url.scheme != "http" or url.hostname not in ("127.0.0.1", "localhost", "::1") or url.username or url.password:
            raise ValueError("Edge backend must be loopback or an explicit loopback SSH tunnel")
        self.endpoint = endpoint.rstrip("/") + "/chat/completions"
        self.contract = contract
        self.allowed_stages = tuple(stages)
        self.provenance = validate(json.loads(Path(provenance_path).read_text()), contract)
        if self.provenance["precision"] != "Q8_0 text / F16 vision":
            raise ValueError("Only the tested Q8_0 export is allowed by the edge router")
        self.model_alias = self.provenance["model_alias"]
        self.model_name = "nvidia/Cosmos-Reason2-2B/automotive-adapter-q8_0-f16"
        self.adapter = self.provenance["source_export"]["adapter"]
        self.model_config_sha256 = self.provenance["source_export"]["files"]["model_config"]["sha256"]
        self.scoring_method = SCORING_METHOD
        self.vision_patch_embedding = "llamacpp-native"
        self.gpu_name = "IQ9 Hexagon decoder / CPU vision (see device startup log)"
        self.lock = threading.Lock()
        self.warmed = False

    def inspect(self, request):
        if request.identity["stage"] not in self.allowed_stages:
            raise ValueError("Stage is outside this adapter's configured route")
        reply = super().inspect(request)
        reply["source"] = "evk_hexagon"
        return reply


class EdgeRouter:
    source = "evk_hexagon"
    dtype = "Q8_0 text / F16 vision"
    scoring_method = SCORING_METHOD
    vision_patch_embedding = "llamacpp-native"
    gpu_name = "IQ9 Hexagon decoder / CPU vision"

    def __init__(self, primary, weld, contract):
        self.contract = contract
        self.models = {"primary": primary, "weld": weld}
        self.routes = {stage: weld if stage == "weld" else primary for stage in contract["stages"]}
        self.stage_backend_aliases = {stage: model.model_alias for stage, model in self.routes.items()}
        self.provenance = {"runtime": "evk-stage-router-q8_0-v1", "models": {k: v.provenance for k, v in self.models.items()},
                           "stage_backend_aliases": self.stage_backend_aliases}
        identity = hashlib.sha256(json.dumps(self.provenance, sort_keys=True).encode()).hexdigest()
        self.model_alias = "Cosmos-Reason2-2B-EVK-Stage-Router-" + identity[:24]
        self.model_name = "nvidia/Cosmos-Reason2-2B/evk-stage-adapters-q8_0-f16"
        self.adapter = {key: model.adapter for key, model in self.models.items()}
        self.model_config_sha256 = primary.model_config_sha256
        self.lock = threading.Lock()

    def inspect(self, request):
        selected = self.routes[request.identity["stage"]]
        reply = selected.inspect(request)
        reply.update(model=self.model_name, backend_alias=self.model_alias,
                     stage_backend_alias=selected.model_alias, routing="resident-merged-weld-override-v1")
        return reply

    def is_ready(self):
        return all(model.is_ready() for model in self.models.values())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--primary-endpoint", default="http://127.0.0.1:18196/v1")
    p.add_argument("--weld-endpoint", default="http://127.0.0.1:18197/v1")
    p.add_argument("--primary-provenance", required=True)
    p.add_argument("--weld-provenance", required=True)
    p.add_argument("--port", type=int, default=18198)
    args = p.parse_args()
    contract = load_gate_contract()
    primary = EdgeModel(args.primary_endpoint, args.primary_provenance, contract, [s for s in contract["stages"] if s != "weld"])
    weld = EdgeModel(args.weld_endpoint, args.weld_provenance, contract, ["weld"])
    for model in (primary, weld):
        model.verify_backend_identity()
        model.warmup()
    router = EdgeRouter(primary, weld, contract)
    print(json.dumps({"source": router.source, "stage_backend_aliases": router.stage_backend_aliases, "ready": router.is_ready()}), flush=True)
    BoundedHTTPServer(("127.0.0.1", args.port), make_handler(router)).serve_forever()


if __name__ == "__main__":
    main()
