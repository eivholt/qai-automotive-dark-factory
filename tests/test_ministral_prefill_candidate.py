"""Reject accidental changes to the KV-only candidate's retained tensors."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

script_dir = Path(__file__).resolve().parents[1] / "scripts/agent"
sys.path.insert(0, str(script_dir))
try:
    spec = importlib.util.spec_from_file_location("ministral_prefill", script_dir / "prepare_ministral_prefill.py")
    candidate = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(candidate)
finally:
    sys.path.remove(str(script_dir))


class PrefillCandidateTests(unittest.TestCase):
    def test_compile_fails_before_source_access_or_sdk_import(self):
        arguments = ["prepare_ministral_prefill.py", "--source-export", "missing-source",
                     "--output", "missing-output", "--sdk-root", "missing-sdk", "--compile"]
        with patch.object(sys, "argv", arguments), patch.object(candidate, "audit") as audit:
            with self.assertRaisesRegex(ValueError, "serialization"):
                candidate.main()
            audit.assert_not_called()

    def outputs(self, logits=True):
        rows = [{"name": name, "dimensions": [8, 1, 128, 64] if name.startswith("past_key")
                 else [8, 1, 64, 128], "dtype": "QNN_DATATYPE_FLOAT_16"}
                for name in sorted(candidate.KV_NAMES)]
        if logits:
            rows.append({"name": "logits", "dimensions": [1, 64, 131072],
                         "dtype": "QNN_DATATYPE_FLOAT_16"})
        return rows

    def test_original_and_prefill_outputs(self):
        candidate.validate_outputs(self.outputs(), has_logits=True)
        candidate.validate_outputs(self.outputs(False), has_logits=False)

    def test_kv_shape_type_name_and_inventory_fail_closed(self):
        variants = []
        rows = self.outputs(False)
        rows[0]["dimensions"] = [8, 1, 64, 128]
        variants.append(rows)
        rows = self.outputs(False)
        rows[0]["dtype"] = "QNN_DATATYPE_FLOAT_32"
        variants.append(rows)
        variants.append(self.outputs(False)[:-1])
        variants.append(self.outputs())
        variants.append(self.outputs(False) + [self.outputs(False)[0]])
        for rows in variants:
            with self.subTest(rows=rows[0]):
                with self.assertRaises(ValueError):
                    candidate.validate_outputs(rows, has_logits=False)

    def test_single_row_ar64_head_is_not_accepted(self):
        rows = self.outputs()
        rows[-1]["dimensions"] = [1, 1, 131072]
        with self.assertRaises(ValueError):
            candidate.validate_outputs(rows, has_logits=True)

    def test_preserved_weights_and_encodings(self):
        before = {"model.lm_head.weight": {"data_sha256": "head"},
                  "key.weight": {"data_sha256": "data", "encoding_sha256": "quant"}}
        after = {"key.weight": copy.deepcopy(before["key.weight"])}
        candidate.validate_retained(before, after)
        for field in ("data_sha256", "encoding_sha256"):
            changed = copy.deepcopy(after)
            changed["key.weight"][field] = "different"
            with self.assertRaises(ValueError):
                candidate.validate_retained(before, changed)
        for invalid in ({}, before, {"unrecognized.weight": {}}):
            with self.assertRaises(ValueError):
                candidate.validate_retained(before, invalid)

    def test_compile_source_and_extensions_are_bound(self):
        with tempfile.TemporaryDirectory() as temp:
            source = Path(temp) / "original"
            source.mkdir()
            manifest = []
            for name in ("genie_config.json", "artifacts/backend_extensions.json",
                         "artifacts/source-tekken-tokenizer.json", "artifacts/split_model_1.bin"):
                path = source / "genie" / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"original")
                manifest.append({"path": name, "sha256": candidate.sha256(path)})
            (source / "provenance.json").write_text(json.dumps({"export_files": manifest}))
            provenance = {"source_export": str(source),
                          "source_provenance_sha256": candidate.sha256(source / "provenance.json")}
            candidate.validate_source_snapshot(source, provenance)
            with self.assertRaisesRegex(ValueError, "differs from preparation"):
                candidate.validate_source_snapshot(Path(temp) / "other", provenance)
            (source / "genie/artifacts/backend_extensions.json").write_bytes(b"changed")
            with self.assertRaisesRegex(ValueError, "artifact changed"):
                candidate.validate_source_snapshot(source, provenance)


if __name__ == "__main__":
    unittest.main()
