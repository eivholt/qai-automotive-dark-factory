"""Candidate preparation never silently changes source precision or provenance."""
import copy
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import tarfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("hexagon_candidate", Path(__file__).resolve().parents[1] /
                                             "scripts/agent/prepare_hexagon_ministral.py")
candidate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(candidate)


def gguf_header(prompt="Fixed prompt", decoder_type=2):
    def string(text):
        data = text.encode()
        return struct.pack("<Q", len(data)) + data
    metadata = [("general.architecture", 8, string("mistral3")),
                ("mistral3.block_count", 4, struct.pack("<I", 26)),
                ("tokenizer.chat_template", 8, string(prompt))]
    tensors = [("token_embd.weight", [3072, 131072], 8)]
    tensors += [(f"norm.{i}", [3072], 0) for i in range(53)]
    tensors += [(f"blk.{i//7}.weight.{i%7}", [3072, 3072], decoder_type) for i in range(182)]
    data = b"GGUF" + struct.pack("<IQQ", 3, len(tensors), len(metadata))
    for key, kind, value in metadata:
        data += string(key) + struct.pack("<I", kind) + value
    for name, shape, kind in tensors:
        data += string(name) + struct.pack("<I", len(shape))
        data += b"".join(struct.pack("<Q", dim) for dim in shape) + struct.pack("<IQ", kind, 0)
    return data


class CandidateTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def fixture(self):
        path = self.root / candidate.CANDIDATE_NAME
        path.write_bytes(gguf_header())
        summary = candidate.gguf_summary(path)
        report = {"schema": candidate.SCHEMA, "file": candidate.CANDIDATE_NAME,
                  "sha256": candidate.sha_file(path), "source": {"kind": "bf16", **candidate.SOURCE_SPECS["bf16"]},
                  "same_as_deployed_q4_k_m": False, "runtime_revision": candidate.RUNTIME_REVISION,
                  "target_quantization": "Q4_0 decoder; Q8_0 tied embedding/head on CPU",
                  "source_summary": summary, "output_summary": summary}
        (self.root / "candidate.json").write_text(json.dumps(report))
        return path, report

    def test_bf16_never_enables_requantization_and_q8_fallback_is_explicit(self):
        common = ["quantizer", "--pure", "--token-embedding-type", "q8_0", "--output-tensor-type", "q8_0"]
        self.assertEqual(candidate.quantizer_arguments("quantizer", "source", "target", "bf16", 8),
                         [*common, "source", "target", "Q4_0", "8"])
        self.assertEqual(candidate.quantizer_arguments("quantizer", "source", "target", "q8_0", 8),
                         [*common, "--allow-requantize", "source", "target", "Q4_0", "8"])

    def test_model_head_and_decoder_types_verified(self):
        path, report = self.fixture()
        candidate.validate_model(report["output_summary"], candidate=True)
        for name, value in (("token_embd.weight", 2), ("blk.0.weight.0", 12), ("norm.0", 1)):
            summary = copy.deepcopy(report["output_summary"])
            summary["tensors"][name]["type"] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                candidate.validate_model(summary, candidate=True)

    def test_metadata_fingerprint_detects_prompt_changes(self):
        path, report = self.fixture()
        path.write_bytes(gguf_header(prompt="Changed prompt"))
        changed = candidate.gguf_summary(path)
        self.assertNotEqual(changed["preserved_metadata_sha256"], report["output_summary"]["preserved_metadata_sha256"])

    def test_model_hash_and_provenance_mismatch_rejected(self):
        path, report = self.fixture()
        self.assertEqual(candidate.verify(self.root), report)
        path.write_bytes(path.read_bytes() + b"modified")
        with self.assertRaises(ValueError):
            candidate.verify(self.root)
        path, report = self.fixture()
        report["same_as_deployed_q4_k_m"] = True
        (self.root / "candidate.json").write_text(json.dumps(report))
        with self.assertRaises(ValueError):
            candidate.verify(self.root)

    def test_wrong_source_cannot_trigger_download_or_conversion(self):
        source = self.root / "wrong.gguf"
        source.write_bytes(gguf_header())
        with patch.object(candidate.subprocess, "run") as run:
            with self.assertRaisesRegex(ValueError, "pinned official"):
                candidate.source_file(self.root, "bf16", source, download=True)
            run.assert_not_called()

    def test_missing_source_requires_explicit_download(self):
        with patch.object(candidate.subprocess, "run") as run:
            with self.assertRaises(ValueError):
                candidate.source_file(self.root, "bf16")
            run.assert_not_called()

    def test_truncated_or_non_gguf_is_rejected(self):
        path = self.root / "bad.gguf"
        for data in (b"not a model", b"GGUF" + struct.pack("<I", 3), gguf_header()[:-20]):
            path.write_bytes(data)
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                candidate.gguf_summary(path)

    def test_archive_runtime_checks_exact_member_contents_not_directory_name(self):
        source = self.root / "source"
        source.mkdir()
        (source / "CMakeLists.txt").write_bytes(b"pinned source")
        (source / "build").mkdir()  # Private generated build outputs are allowed.
        archive = self.root / "source.tar.gz"
        with tarfile.open(archive, "w:gz") as package:
            member = tarfile.TarInfo("llama.cpp-" + candidate.RUNTIME_REVISION + "/CMakeLists.txt")
            member.size = len(b"pinned source")
            package.addfile(member, io.BytesIO(b"pinned source"))
        with patch.object(candidate, "RUNTIME_ARCHIVE_SHA256", candidate.sha_file(archive)):
            result = candidate.verify_runtime_source(source, archive)
            self.assertEqual(result["tracked_members_verified"], 1)
            self.assertEqual(result["method"], "official_archive_members")
            (source / "CMakeLists.txt").write_bytes(b"edited source")
            with self.assertRaisesRegex(ValueError, "source content differs"):
                candidate.verify_runtime_source(source, archive)
        with self.assertRaisesRegex(ValueError, "hash-verified"):
            candidate.verify_runtime_source(source, archive)


if __name__ == "__main__":
    unittest.main()
