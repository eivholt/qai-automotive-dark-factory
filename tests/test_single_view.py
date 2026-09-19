"""Single-view experiments must not silently reuse whole-scene permissions."""
import importlib.util
import io
import json
from pathlib import Path
import sys
import unittest
from types import SimpleNamespace
from unittest.mock import patch

from dark_factory.gates import load_gate_contract
from dark_factory.gates_single_view import aggregate, prompt_for, prompt_hashes

SCRIPTS = Path(__file__).resolve().parents[1] / "scripts/model"
spec = importlib.util.spec_from_file_location("single_view_probe", SCRIPTS / "probe_edge_reason2.py")
probe = importlib.util.module_from_spec(spec)
with patch.object(sys, "path", [str(SCRIPTS), *sys.path]):
    spec.loader.exec_module(probe)


class SingleViewTests(unittest.TestCase):
    @staticmethod
    def views(labels):
        return [{"camera_id": f"spot{i+1}", "decision": label} for i, label in enumerate(labels)]

    def test_presence_needs_unanimous_branch(self):
        for labels, expected in [("GGG", "G"), ("RRR", "R"), ("GRG", "A"), ("RRA", "A")]:
            self.assertEqual(aggregate("panel_presence", self.views(labels))[0], expected)

    def test_defect_in_one_view_prevents_green(self):
        for stage in ("adhesive", "weld", "panel_seated"):
            for labels, expected in [("GGG", "G"), ("GRG", "R"), ("GAG", "A"), ("ARG", "R")]:
                self.assertEqual(aggregate(stage, self.views(labels))[0], expected)

    def test_missing_duplicate_or_failed_view_holds(self):
        rows = self.views("GGG")
        for invalid in (rows[:2], [rows[0], rows[0], rows[2]], list(reversed(rows)),
                        [rows[0], {**rows[1], "error": "timeout"}, rows[2]]):
            self.assertEqual(aggregate("weld", invalid)[0], "A")

    def test_prompts_explicitly_differ_from_three_view_contract(self):
        contract = load_gate_contract()
        for stage, digest in prompt_hashes().items():
            self.assertNotEqual(digest, contract["stages"][stage]["prompt_sha256"])
            self.assertIn("single", prompt_for(stage))
            self.assertNotIn("Inspect three", prompt_for(stage))

    def test_payload_contains_exactly_selected_grayscale_image(self):
        from PIL import Image
        pngs = []
        for pixel in (10, 20, 30):
            output = io.BytesIO()
            Image.new("L", (448, 256), pixel).save(output, format="PNG")
            pngs.append(output.getvalue())
        request = SimpleNamespace(pngs=pngs, identity={"stage": "weld"})
        content = probe.image_content(request, load_gate_contract(), image_index=1, prompt=prompt_for("weld"))
        images = [item for item in content if item["type"] == "image_url"]
        self.assertEqual(len(images), 1)
        import base64
        self.assertEqual(base64.b64decode(images[0]["image_url"]["url"].split(",")[1]), pngs[1])
        self.assertEqual(content[-1]["text"], prompt_for("weld"))
        self.assertNotIn("expected", str(content))

    def test_single_view_transport_requests_one_output_token_once(self):
        from PIL import Image
        output = io.BytesIO()
        Image.new("L", (448, 256), 20).save(output, format="PNG")
        request = SimpleNamespace(pngs=[output.getvalue()] * 3, identity={"stage": "weld"})
        candidates = [
            {"token": "G", "logprob": -0.001}, {"token": "R", "logprob": -8}, {"token": "A", "logprob": -9}
        ] + [{"token": f"other{i}", "logprob": -14} for i in range(17)]
        response = {"model": "test-model", "choices": [{"logprobs": {"content": [{"top_logprobs": candidates}]}}]}
        replies = [io.BytesIO(json.dumps({"data": [{"id": "test-model"}]}).encode()),
                   io.BytesIO(json.dumps(response).encode())]
        with patch.object(probe, "urlopen", side_effect=replies) as send:
            result = probe.probe("http://127.0.0.1:18199/v1", {"model_alias": "test-model"},
                                 request, load_gate_contract(), 5, image_index=2)
        self.assertEqual(result["decision"], "G")
        self.assertEqual(len(result["image_sha256"]), 1)
        self.assertEqual(send.call_count, 2)  # Identity GET, then one inference POST.
        body = json.loads(send.call_args_list[1].args[0].data)
        self.assertEqual(body["max_tokens"], 1)
        self.assertEqual(sum(item["type"] == "image_url" for item in body["messages"][0]["content"]), 1)
        self.assertFalse(body["cache_prompt"])
