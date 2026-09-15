import base64
import copy
import json
import math
import struct
import tempfile
import unittest
import zlib
from pathlib import Path
from dark_factory.gates import GateLatch, load_gate_contract, parse_gate_request, hold_reply, select_decision, camera_content, MESSAGE_LAYOUT
from dark_factory.protocol import ProtocolError


def gray_png(width=448, height=256):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress((b"\0" + bytes([30]) * width) * height)) + chunk(b"IEND", b"")


class GateTests(unittest.TestCase):
    def setUp(self):
        self.c = load_gate_contract()
        self.payload = {"contract_version": self.c["version"], "request_id": "r1", "body_id": "b1", "stage": "panel_presence", "captured_at_ms": 100,
            "images": [{"camera_id": k, "png_base64": base64.b64encode(gray_png()).decode()} for k in ["spot1", "spot2", "spot3"]]}

    def reply(self, payload=None, decision="G"):
        result = hold_reply(payload or self.payload, self.c, "test", valid=True)
        result["decision"] = decision
        return result

    def test_each_camera_image_has_a_text_separator(self):
        images = [{"type": "image", "image": "left"}, {"type": "image", "image": "right"}]
        content = camera_content(images, "Fixed prompt")
        self.assertEqual(content, [images[0], {"type": "text", "text": "\n"}, images[1], {"type": "text", "text": "Fixed prompt"}])
        self.assertEqual(len(images), 2)
        self.assertEqual(self.reply()["message_layout"], MESSAGE_LAYOUT)

    def test_all_stage_contracts_validate(self):
        for stage, spec in self.c["stages"].items():
            self.payload["stage"] = stage
            self.payload["images"] = [{"camera_id": k, "png_base64": base64.b64encode(gray_png()).decode()} for k in spec["camera_ids"]]
            parsed = parse_gate_request(self.payload, self.c)
            self.assertEqual(parsed.camera_ids, tuple(spec["camera_ids"]))
            self.assertNotIn("images", parsed.identity)

    def test_no_labels_seed_ground_truth_or_custom_prompt_allowed(self):
        for field in ("ground_truth", "seed", "temperature_c", "prompt", "expected", "case"):
            with self.subTest(field=field):
                bad = {**self.payload, field: "G"}
                with self.assertRaises(ProtocolError):
                    parse_gate_request(bad, self.c)

    def test_camera_batch_must_be_complete_and_ordered(self):
        for images in ([], self.payload["images"][:1], list(reversed(self.payload["images"])), self.payload["images"] * 2):
            with self.assertRaises(ProtocolError):
                parse_gate_request({**self.payload, "images": images}, self.c)

    def test_invalid_identity(self):
        for field, value in (("body_id", "../body"), ("request_id", ""), ("stage", "old-tray"), ("stage", []),
                             ("contract_version", "old"), ("captured_at_ms", -1), ("captured_at_ms", True),
                             ("captured_at_ms", float("nan")), ("captured_at_ms", float("inf"))):
            with self.subTest(field=field, value=value), self.assertRaises(ProtocolError):
                parse_gate_request({**self.payload, field: value}, self.c)

    def test_reject_malformed_base64_png_size_and_metadata(self):
        for encoded in ("bad data", base64.b64encode(gray_png(224, 128)).decode(), base64.b64encode(b"not png").decode()):
            bad = copy.deepcopy(self.payload)
            bad["images"][0]["png_base64"] = encoded
            with self.assertRaises(ProtocolError):
                parse_gate_request(bad, self.c)
        bad = copy.deepcopy(self.payload)
        bad["images"][0]["label"] = "G"
        with self.assertRaises(ProtocolError):
            parse_gate_request(bad, self.c)

    def test_prompt_hash_and_temperature_mapping_are_locked(self):
        for change in (lambda c: c["stages"]["panel_presence"].update(prompt="Different"), lambda c: c.update(thermal_window_c=[0, 100])):
            c = copy.deepcopy(self.c)
            change(c)
            with tempfile.TemporaryDirectory() as d:
                path = Path(d) / "contract.json"
                path.write_text(json.dumps(c))
                with self.assertRaises(ProtocolError):
                    load_gate_contract(path)

    def test_unsupported_policy_model_and_generation_rejected(self):
        mutations = [("decision_policy", "consecutive_green", 3), ("decision_policy", "consecutive_green", True),
                     ("decision_policy", "max_reply_age_seconds", .1), ("decision_policy", "max_reply_age_seconds", float("nan")),
                     ("decision_policy", "minimum_label_mass", True), ("decision_policy", "minimum_label_mass", .4), ("decision_policy", "minimum_conditional_probability", .4), ("decision_policy", "minimum_conditional_probability", float("inf")),
                     ("generation", "method", "free_text"), (None, "model_family", "other")]
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / "contract.json"
            for parent, key, value in mutations:
                c = copy.deepcopy(self.c)
                (c[parent] if parent else c)[key] = value
                path.write_text(json.dumps(c))
                with self.subTest(parent=parent, key=key, value=value), self.assertRaises(ProtocolError):
                    load_gate_contract(path)

    def test_uncertain_logits_hold(self):
        p = self.c["decision_policy"]
        self.assertEqual(select_decision({"G": .99, "A": .005, "R": .005}, .9, p)[0], "G")
        self.assertEqual(select_decision({"G": .96, "A": .02, "R": .02}, .9, p)[0], "A")
        self.assertEqual(select_decision({"G": .96, "A": .02, "R": .02}, .1, p)[0], "A")
        self.assertEqual(select_decision({"G": .4, "A": .2, "R": .4}, .9, p)[0], "A")
        self.assertEqual(select_decision({"G": .01, "A": .01, "R": .98}, .9, p)[0], "R")
        for scores, mass in (({"G": 1}, 1), ({"G": math.nan, "A": 0, "R": 0}, 1), ({"G": 1, "A": 1, "R": 1}, 1), ({"G": 1, "A": 0, "R": 0}, 2)):
            self.assertEqual(select_decision(scores, mass, p)[0], "A")

    def test_requires_two_new_frames_and_no_replayed_reply(self):
        latch = GateLatch(self.c)
        self.assertEqual(latch.observe(self.reply(), self.payload, 200), "A")
        second = {**self.payload, "request_id": "r2", "captured_at_ms": 300}
        self.assertEqual(latch.observe(self.reply(second), second, 400), "G")
        self.assertEqual(latch.observe(self.reply(second), second, 450), "A")

    def test_stale_wrong_stage_hash_camera_or_transport_always_hold(self):
        for field, value in (("request_id", "other"), ("body_id", "other"), ("stage", "weld"), ("prompt_sha256", "bad"),
                             ("message_layout", "adjacent-images"), ("decision_policy", {}), ("camera_ids", ["left"]), ("modality", "thermal"), ("valid", False), ("decision", "GREEN")):
            latch = GateLatch(self.c)
            reply = self.reply()
            reply[field] = value
            self.assertEqual(latch.observe(reply, self.payload, 200), "A")
        self.assertEqual(GateLatch(self.c).observe(self.reply(), self.payload, 9000), "A")
        self.assertEqual(GateLatch(self.c).observe(self.reply(), self.payload, 50), "A")

    def test_seating_red_immediate_and_new_body_resets_green_history(self):
        self.payload["stage"] = "panel_seated"
        latch = GateLatch(self.c)
        self.assertEqual(latch.observe(self.reply(decision="R"), self.payload, 200), "R")
        second = {**self.payload, "request_id": "r2", "captured_at_ms": 300}
        self.assertEqual(latch.observe(self.reply(second), second, 400), "A")
        third = {**second, "request_id": "r3", "captured_at_ms": 500, "body_id": "new-body"}
        self.assertEqual(latch.observe(self.reply(third), third, 600), "A")

    def test_presence_red_requires_two_matching_fresh_frames(self):
        latch = GateLatch(self.c)
        first = self.payload
        self.assertEqual(latch.observe(self.reply(first, "R"), first, 200), "A")
        self.assertEqual(latch.observe(self.reply(first, "R"), first, 200), "A")
        def send(i, label, body="b1"):
            p = {**first, "request_id": f"r{i}", "captured_at_ms": i*100, "body_id": body}
            return latch.observe(self.reply(p, label), p, i*100+10)
        self.assertEqual(send(2, "R"), "A")
        self.assertEqual(send(3, "G"), "A")
        self.assertEqual(send(4, "R"), "A")
        self.assertEqual(send(5, "R"), "R")
        self.assertEqual(send(6, "R", "new-body"), "A")
        self.assertEqual(send(7, "A", "new-body"), "A")
        self.assertEqual(send(8, "R", "new-body"), "A")

    def test_v1_door_contract_and_request_are_rejected(self):
        with self.assertRaises(ProtocolError):
            parse_gate_request({**self.payload, "contract_version": "automotive-process-v1", "stage": "doors"}, self.c)
        c = copy.deepcopy(self.c)
        c["version"] = "automotive-process-v1"
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/"legacy.json"
            path.write_text(json.dumps(c), encoding="utf-8")
            with self.assertRaises(ProtocolError):
                load_gate_contract(path)

    def test_three_bead_panel_v2_contract_and_request_are_rejected(self):
        with self.assertRaises(ProtocolError):
            parse_gate_request({**self.payload, "contract_version": "automotive-panel-process-v2"}, self.c)
        c = copy.deepcopy(self.c)
        c["version"] = "automotive-panel-process-v2"
        with tempfile.TemporaryDirectory() as d:
            path = Path(d)/"three-bead-panel.json"
            path.write_text(json.dumps(c), encoding="utf-8")
            with self.assertRaises(ProtocolError):
                load_gate_contract(path)

    def test_errors_are_uniform_holds(self):
        for payload in (None, {}, self.payload, {"stage": []}):
            result = hold_reply(payload, self.c, "error")
            self.assertFalse(result["valid"])
            self.assertEqual(result["decision"], "A")
            self.assertIn("request_id", result)
            self.assertIn("prompt_sha256", result)

if __name__ == "__main__":
    unittest.main()
