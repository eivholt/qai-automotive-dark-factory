import base64
import copy
import io
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/model"))
from dark_factory.gates import load_gate_contract, parse_gate_request
from dark_factory.gates_stream import StreamCoordinator, STREAM_PROTOCOL
from dark_factory.protocol import ProtocolError
from serve_streamed_reason2 import validate_stream_evidence


class LiveStreamTests(unittest.TestCase):
    def setUp(self):
        self.contract = load_gate_contract()
        self.now = 0.
        self.calls = []
        self.stream = StreamCoordinator(self.contract, self.native, lambda: self.now)
        self.envelope = dict(contract_version=self.contract["version"], request_id="request-1", body_id="body-1",
            stage="panel_presence", captured_at_ms=1234, session_id="session-1", epoch=1,
            prompt_sha256=self.contract["stages"]["panel_presence"]["prompt_sha256"])
        self.images = []
        for index in range(3):
            data = io.BytesIO()
            Image.new("L", (448, 256), 20 + index).save(data, format="PNG")
            self.images.append(dict(camera_id=f"spot{index+1}", png_base64=base64.b64encode(data.getvalue()).decode()))

    def native(self, role, op, **fields):
        self.calls.append((role, op, fields))
        if op == "append":
            self.assertTrue(Path(fields["image"]).is_file())
            return dict(prepared=True, received=fields["index"]+1, early_prefill_tokens=128 if fields["index"] else 0)
        if op == "score":
            return dict(early_prefill_tokens=128, visual_tokens=336, decision="R")
        return {}

    def prepare(self, index, envelope=None):
        return self.stream.prepare(dict(envelope or self.envelope, image=self.images[index]))

    def final(self, envelope=None):
        e = envelope or self.envelope
        return parse_gate_request({**{k: e[k] for k in ("contract_version", "request_id", "body_id", "stage", "captured_at_ms")},
                                   "images": self.images}, self.contract)

    def test_only_complete_original_batch_can_score(self):
        for index in range(3):
            ack = self.prepare(index)
            self.assertNotIn("decision", ack)
            self.assertNotIn("valid", ack)
            self.assertEqual(ack["protocol"], STREAM_PROTOCOL)
        self.assertEqual([c[1] for c in self.calls], ["begin", "append", "append", "append"])
        result, metadata = self.stream.finish(self.final(), "session-1", 1)
        self.assertEqual(result["decision"], "R")
        self.assertEqual(metadata["prepared_images"], 3)
        self.assertIsNone(self.stream.active)
        with self.assertRaises(ProtocolError):
            self.stream.finish(self.final(), "session-1", 1)
        self.assertEqual(sum(c[1] == "score" for c in self.calls), 1)
        self.assertEqual(self.calls[0][2]["prompt"], self.contract["stages"]["panel_presence"]["prompt"])
        self.assertNotIn("body_id", self.calls[0][2])

    def test_incomplete_and_changed_final_pixels_never_score(self):
        self.prepare(0)
        with self.assertRaises(ProtocolError):
            self.stream.finish(self.final(), "session-1", 1)
        self.envelope["epoch"] = 2
        for index in range(3):
            self.prepare(index)
        self.images[1]["png_base64"] = self.images[0]["png_base64"]
        with self.assertRaises(ProtocolError):
            self.stream.finish(self.final(), "session-1", 2)
        self.assertFalse(any(c[1] == "score" for c in self.calls))

    def test_order_duplicate_and_identity_are_strict(self):
        with self.assertRaises(ProtocolError):
            self.prepare(1)
        self.prepare(0)
        for index, changed in ((0, {}), (2, {}), (1, {"body_id": "other"}), (1, {"request_id": "other"}),
                               (1, {"captured_at_ms": 1235}), (1, {"prompt_sha256": "0"*64})):
            with self.subTest(index=index, changed=changed), self.assertRaises(ProtocolError):
                self.prepare(index, dict(self.envelope, **changed))
        self.assertEqual(len(self.stream.active.hashes), 1)

    def test_cancel_before_prepare_and_late_cancel_do_not_resurrect_or_kill_new_batch(self):
        self.stream.cancel(self.envelope)
        with self.assertRaises(ProtocolError):
            self.prepare(0)
        newer = dict(self.envelope, epoch=2, request_id="new-request")
        self.prepare(0, newer)
        self.stream.cancel(self.envelope)
        self.assertEqual(self.stream.active.envelope, newer)
        for index in (1, 2):
            self.prepare(index, newer)
        self.stream.finish(self.final(newer), "session-1", 2)

    def test_pause_cancel_after_prefill_invalidates_all_work(self):
        self.prepare(0)
        self.prepare(1)
        self.stream.cancel(self.envelope)
        with self.assertRaises(ProtocolError):
            self.prepare(2)
        with self.assertRaises(ProtocolError):
            self.stream.finish(self.final(), "session-1", 1)
        self.assertEqual(self.calls[-1][1], "cancel")

    def test_new_epoch_replaces_only_same_session(self):
        self.prepare(0)
        with self.assertRaises(ProtocolError):
            self.prepare(0, dict(self.envelope, session_id="other", epoch=2))
        newer = dict(self.envelope, epoch=2, request_id="new-request")
        self.prepare(0, newer)
        self.assertEqual([c[1] for c in self.calls], ["begin", "append", "cancel", "begin", "append"])
        with self.assertRaises(ProtocolError):
            self.prepare(1)

    def test_expiry_and_wrong_final_identity_hold(self):
        for index in range(3):
            self.prepare(index)
        for changes in ({"body_id": "other"}, {"request_id": "other"}, {"stage": "weld"}):
            with self.assertRaises(ProtocolError):
                self.stream.finish(self.final(dict(self.envelope, **changes)), "session-1", 1)
        self.now = 8
        with self.assertRaises(ProtocolError):
            self.stream.finish(self.final(), "session-1", 1)
        self.assertIsNone(self.stream.active)
        self.assertFalse(any(c[1] == "score" for c in self.calls))

    def test_grayscale_and_envelope_allowlist(self):
        for changes in ({"epoch": True}, {"epoch": 0}, {"epoch": 1.5}, {"ground_truth": "missing"}, {"session_id": ""}):
            with self.subTest(changes=changes), self.assertRaises(ProtocolError):
                self.prepare(0, dict(self.envelope, **changes))
        data = io.BytesIO()
        Image.new("RGB", (448, 256), (10, 20, 30)).save(data, format="PNG")
        self.images[0]["png_base64"] = base64.b64encode(data.getvalue()).decode()
        with self.assertRaises(ProtocolError):
            self.prepare(0)
        self.assertEqual(self.calls, [])

    def test_thermal_stage_selects_only_stage_routed_weights(self):
        self.envelope.update(stage="weld", prompt_sha256=self.contract["stages"]["weld"]["prompt_sha256"])
        self.prepare(0)
        self.assertEqual([c[0] for c in self.calls], ["weld", "weld"])

    def test_deployment_requires_full_matching_numerical_evidence(self):
        provenance = dict(schema="automotive-qairt-candidate-v1", role="primary",
            stages=["panel_presence", "adhesive", "panel_seated"], training_report={}, bundle_sha256={"model": "sha"})
        result = dict(execution="offline-stream-prefill-v1", scoring_method="gar-full-vocabulary-softmax-v1",
            generated_tokens=1, vocab_size=151936, label_probabilities={"G": .999, "A": .0005, "R": .0005},
            early_prefill_tokens=128, visual_tokens=336, prompt_tokens=500, raw_top_id=38, decision="G")
        evidence = dict(complete=True, mode="parity", incremental=True, worker_sha256="worker",
            bundle_sha256=provenance["bundle_sha256"], decision_policy=self.contract["decision_policy"], rows=[])
        for stage, count in (("panel_presence", 24), ("panel_seated", 24), ("adhesive", 72)):
            config = self.contract["stages"][stage]
            for index in range(count):
                evidence["rows"].append(dict(sample_id=f"{stage}-{index}", stage=stage, expected="G",
                    source_prompt_sha256=config["prompt_sha256"], candidate_prompt=config["prompt"],
                    candidate_prompt_sha256=config["prompt_sha256"], baseline=copy.deepcopy(result), candidate=copy.deepcopy(result)))
        with patch("serve_qairt_reason2.validate_adapter_training"):
            validate_stream_evidence(provenance, evidence, "worker", "primary", self.contract)
            for key, value in (("complete", False), ("worker_sha256", "other"), ("bundle_sha256", {}),
                               ("incremental", False), ("rows", evidence["rows"][:-1])):
                with self.subTest(key=key), self.assertRaises(ValueError):
                    validate_stream_evidence(provenance, dict(evidence, **{key: value}), "worker", "primary", self.contract)
            for key, value in (("raw_top_id", 49), ("prompt_tokens", 499), ("early_prefill_tokens", 0),
                               ("label_probabilities", {"G": .998, "A": .001, "R": .001})):
                changed = copy.deepcopy(evidence)
                changed["rows"][0]["candidate"][key] = value
                with self.subTest(key=key), self.assertRaises(ValueError):
                    validate_stream_evidence(provenance, changed, "worker", "primary", self.contract)


if __name__ == "__main__":
    unittest.main()
