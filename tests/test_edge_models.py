"""Reject mismatched multimodal exports before device experiments."""
import copy
import hashlib
import importlib.util
from pathlib import Path
import unittest
from types import SimpleNamespace
from unittest.mock import patch
import sys
from dark_factory.gates import load_gate_contract, MESSAGE_LAYOUT

spec = importlib.util.spec_from_file_location("edge_provenance", Path(__file__).resolve().parents[1] / "scripts/model/record_edge_provenance.py")
edge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(edge)
serve_spec = importlib.util.spec_from_file_location("serve_edge", Path(__file__).resolve().parents[1] / "scripts/model/serve_edge_reason2.py")
serve = importlib.util.module_from_spec(serve_spec)
with patch.dict(sys.modules, {"record_edge_provenance": edge}):
    serve_spec.loader.exec_module(serve)


class EdgeModelTests(unittest.TestCase):
    def report(self):
        c = load_gate_contract()
        training = {"contract_version": c["version"], "message_layout": MESSAGE_LAYOUT,
                    "prompt_hashes": {s: v["prompt_sha256"] for s, v in c["stages"].items()},
                    "image_contract": c["image"], "thermal_window_c": c["thermal_window_c"],
                    "training_stages": list(c["stages"])}
        source = {"model": "nvidia/Cosmos-Reason2-2B", "adapter": "automotive-adapter",
                  "adapter_provenance": {**training, "training_report": training},
                  "files": {"vision_f16": {"sha256": "1" * 64}, "text_bf16": {"sha256": "2" * 64}}}
        report = {"schema": "automotive-edge-gguf-v1", "precision": "Q8_0 text / F16 vision",
                  "source_export": source, "files": {"text": {"sha256": "3" * 64}, "vision": {"sha256": "1" * 64}}}
        report["model_alias"] = "automotive-reason2-q8_0-" + hashlib.sha256(("3" * 64 + "1" * 64).encode()).hexdigest()[:24]
        return report, c

    def test_matching_multimodal_adapter(self):
        r, c = self.report()
        self.assertEqual(edge.validate(r, c), r)

    def test_other_adapter_vision_encoder_rejected_even_with_new_alias(self):
        r, c = self.report()
        r["files"]["vision"]["sha256"] = "4" * 64
        r["model_alias"] = "automotive-reason2-q8_0-" + hashlib.sha256(("3" * 64 + "4" * 64).encode()).hexdigest()[:24]
        with self.assertRaisesRegex(ValueError, "matching adapted"):
            edge.validate(r, c)

    def test_prompt_or_stage_mismatch_rejected(self):
        r, c = self.report()
        bad_prompt = copy.deepcopy(r)
        bad_prompt["source_export"]["adapter_provenance"]["prompt_hashes"]["weld"] = "old"
        bad_scope = copy.deepcopy(r)
        bad_scope["source_export"]["adapter_provenance"]["training_report"]["training_stages"] = ["weld"]
        for invalid in (bad_prompt, bad_scope):
            with self.assertRaises(ValueError):
                edge.validate(invalid, c)

    def test_alias_cannot_be_reused_for_other_decoder(self):
        r, c = self.report()
        r["files"]["text"]["sha256"] = "5" * 64
        with self.assertRaisesRegex(ValueError, "alias"):
            edge.validate(r, c)

    def test_stage_routing_does_not_depend_on_decision(self):
        c = load_gate_contract()
        def model(name, decision):
            return SimpleNamespace(model_alias=name, adapter=name, provenance={"name": name},
                                   model_config_sha256="same-base", is_ready=lambda: True,
                                   inspect=lambda request: {"decision": decision, "adapter": name})
        primary, weld = model("primary", "R"), model("weld", "G")
        router = serve.EdgeRouter(primary, weld, c)
        for stage in c["stages"]:
            reply = router.inspect(SimpleNamespace(identity={"stage": stage}))
            self.assertEqual(reply["stage_backend_alias"], "weld" if stage == "weld" else "primary")
        primary.inspect = lambda request: {"decision": "G"}
        self.assertEqual(router.inspect(SimpleNamespace(identity={"stage": "adhesive"}))["stage_backend_alias"], "primary")
        self.assertEqual(router.source, "evk_hexagon")
