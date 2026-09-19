"""Calibration isolation and full-vocabulary classification for the NPU experiment."""
import hashlib
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts/model" / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


trainer = load("qairt_trainer", "train_lora.py")
with patch.dict(sys.modules, {"train_lora": trainer}):
    calibration = load("qairt_calibration", "prepare_qairt_calibration.py")
    probe = load("qairt_probe", "probe_qairt_reason2.py")
with patch.dict(sys.modules, {"probe_qairt_reason2": probe}):
    serve = load("qairt_serve", "serve_qairt_reason2.py")


class QairtExperimentTests(unittest.TestCase):
    def test_precision_candidate_does_not_modify_source_checkpoint(self):
        converter = load("qairt_float_vision", "make_qairt_float_vision.py")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source, output = root / "source", root / "candidate"
            source.mkdir()
            enc = {group: [{"name": group, "bw": 8, "dtype": "INT", "scale": [.125]}]
                   for group in ("activation_encodings", "param_encodings")}
            (source / "vision_encoder.encodings").write_text(json.dumps(enc))
            (source / "args.json").write_text('{"image_size":[256,448]}')
            (source / "model.data").write_bytes(b"retained text weights")
            originals = {p.name: p.read_bytes() for p in source.iterdir()}
            with patch.object(sys, "argv", ["converter", "--source", str(source), "--output", str(output)]):
                converter.main()
                with self.assertRaises(ValueError):
                    converter.main()
            self.assertEqual(originals, {p.name: p.read_bytes() for p in source.iterdir()})
            self.assertEqual((source / "model.data").read_bytes(), (output / "model.data").read_bytes())
            converted = json.loads((output / "vision_encoder.encodings").read_text())
            self.assertTrue(all(e["dtype"] == "FLOAT" and e["bw"] == 16
                                for values in converted.values() for e in values))

    def reply(self, probabilities):
        return {"scoring_method": "gar-full-vocabulary-softmax-v1", "generated_tokens": 1,
                "vocab_size": 151936, "label_probabilities": probabilities}

    def test_full_probability_mass_not_renormalized_away(self):
        c = probe.load_gate_contract()
        result = probe.score(self.reply({"G": .4, "A": .0001, "R": .0001}), c)
        self.assertEqual(result["decision"], "A")
        self.assertEqual(result["reason"], "insufficient_label_support")
        self.assertEqual(probe.score(self.reply({"G": .999, "A": .0001, "R": .0001}), c)["decision"], "G")
        self.assertEqual(probe.score(self.reply({"G": .97, "A": .02, "R": .01}), c)["decision"], "A")

    def test_invalid_native_scores_rejected(self):
        c = probe.load_gate_contract()
        for p in ({"G": float("nan"), "A": 0, "R": 0}, {"G": 1, "A": 1, "R": 0}, {"G": True, "A": 0, "R": 0}):
            with self.assertRaises(ValueError):
                probe.score(self.reply(p), c)
        r = self.reply({"G": 1, "A": 0, "R": 0})
        r["generated_tokens"] = 2
        with self.assertRaises(ValueError):
            probe.score(r, c)

    def test_service_rejects_false_acceptance_and_mismatched_validation(self):
        c = probe.load_gate_contract()
        provenance = {"schema": "automotive-qairt-candidate-v1", "role": "primary",
            "stages": ["panel_presence", "adhesive", "panel_seated"], "training_report": {}, "bundle_sha256": {"model": "sha"}}
        rows = []
        for stage, count in (("panel_presence", 24), ("panel_seated", 24), ("adhesive", 72)):
            for i in range(count):
                row = dict(self.reply({"G": .999, "A": .0005, "R": .0005}), sample=f"{stage}-{i}", stage=stage, expected="G", decision="G")
                rows.append(row)
        evidence = {"candidate_provenance": provenance, "worker_sha256": "worker", "bundle_sha256": provenance["bundle_sha256"],
                    "decision_policy": c["decision_policy"], "rows": rows}
        with patch.object(serve, "validate_adapter_training"):
            serve.validate_evidence(provenance, evidence, "worker", "primary", c)
            false_g = copy.deepcopy(evidence)
            false_g["rows"][-1]["expected"] = "R"
            with self.assertRaisesRegex(ValueError, "accuracy"):
                serve.validate_evidence(provenance, false_g, "worker", "primary", c)
            for invalid in (dict(evidence, worker_sha256="changed"), dict(evidence, experiment="single-view"), dict(evidence, rows=rows[:-1])):
                with self.assertRaises(ValueError):
                    serve.validate_evidence(provenance, invalid, "worker", "primary", c)

    def test_native_worker_response_identity_is_required(self):
        with tempfile.TemporaryDirectory() as temp:
            child = Path(temp) / "child.py"
            child.write_text('import json,sys\nprint(json.dumps({"ready":True,"schema":"automotive-qairt-score-worker-v1","label_ids":[38,32,49]}),flush=True)\nfor line in sys.stdin:\n print(json.dumps({"request_id":"wrong"}),flush=True)\n')
            worker = probe.Worker([sys.executable, str(child)], Path(temp) / "worker.log", timeout=10)
            try:
                with self.assertRaisesRegex(ValueError, "identity mismatch"):
                    worker.infer({"request_id": "expected", "images": [], "prompt": ""}, 2)
            finally:
                worker.close()

    def test_calibration_requires_recorded_training_manifest_and_train_split(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            training = root / "train.jsonl"
            training.write_text("{}\n")
            report = root / "training_report.json"
            report.write_text(json.dumps({"training_manifest_sha256": "wrong"}))
            with patch.object(calibration, "validate_adapter_training"), patch.object(calibration, "load_rows") as rows:
                with self.assertRaisesRegex(ValueError, "recorded training"):
                    calibration.prepare(root, training, ["weld"], 3, 1)
                rows.assert_not_called()
                report.write_text(json.dumps({"training_manifest_sha256": hashlib.sha256(training.read_bytes()).hexdigest()}))
                rows.side_effect = ValueError("not train")
                with self.assertRaisesRegex(ValueError, "not train"):
                    calibration.prepare(root, training, ["weld"], 3, 1)
                self.assertEqual(rows.call_args.args[2], "train")

    def test_static_calibration_pairs_and_duplicate_image_exclusion(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest("Pillow is needed for calibration image checks")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            manifest = root / "train.jsonl"
            manifest.write_text("{}\n")
            (root / "training_report.json").write_text(json.dumps({"training_manifest_sha256": hashlib.sha256(manifest.read_bytes()).hexdigest()}))
            rows = []
            for index, label in enumerate("GAR"):
                path = root / f"{label}.png"
                Image.new("L", (448, 256), index * 80).save(path)
                rows.append({"stage": "weld", "answer": label, "images": [str(path)] * 3,
                             "sample_id": label, "episode_id": label, "camera_ids": ["spot1", "spot2", "spot3"]})
            with patch.object(calibration, "validate_adapter_training"), patch.object(calibration, "load_rows", return_value=rows):
                result = calibration.prepare(root, manifest, ["weld"], 3, 1)
                self.assertEqual(len({p["sha256"][0] for p in result["pairs"]}), 3)
                for p in result["pairs"]:
                    self.assertEqual(p["frames"][0], p["frames"][1])
                    self.assertEqual(p["source"]["split"], "train")
                with self.assertRaisesRegex(ValueError, "distinct training"):
                    calibration.prepare(root, manifest, ["weld"], 4, 1)
