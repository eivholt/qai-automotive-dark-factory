from __future__ import annotations

import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from dark_factory.dataset import canonical_hash, file_sha256, generate_manifest, read_jsonl, split_groups, training_records, validate_manifest, write_jsonl
from dark_factory.protocol import load_contract

REPO = Path(__file__).resolve().parents[1]


def png(value=0, width=448, height=256):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = (b"\0" + bytes([value]) * width) * height
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b"")


class DatasetTests(unittest.TestCase):
    def setUp(self):
        self.config = json.loads((REPO / "configs/randomization.json").read_text())
        self.config.update(groups=6, frames_per_episode=2, split_fractions={"train": 0.5, "validation": 0.25, "test": 0.25})
        self.contract = load_contract()
        self.rows = generate_manifest(self.config, self.contract)

    def capture(self, root):
        rows = copy.deepcopy(self.rows)
        for index, row in enumerate(rows):
            path = root / row["image"]
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(png(index))
            row["capture_status"] = "captured"
        return rows

    def test_deterministic_and_balanced(self):
        self.assertEqual(self.rows, generate_manifest(self.config, self.contract))
        report = validate_manifest(self.rows, contract=self.contract)
        self.assertEqual(report["rows"], 36)
        self.assertEqual(report["episodes"], 18)
        for count in report["counts"].values():
            self.assertEqual(len(set(count.values())), 1)

    def test_nuisance_identical_across_labels(self):
        groups = {}
        for row in self.rows:
            value = canonical_hash([row["sensor"], row["randomization"]])
            self.assertEqual(groups.setdefault(row["leakage_group"], value), value)
        self.rows[1]["sensor"]["gain"] += 0.1
        with self.assertRaisesRegex(ValueError, "nuisance"):
            validate_manifest(self.rows)

    def test_split_is_order_independent(self):
        groups = [str(index) for index in range(10)]
        fractions = self.config["split_fractions"]
        self.assertEqual(split_groups(groups, fractions, 10), split_groups(groups[::-1], fractions, 10))

    def test_group_leakage_rejected(self):
        self.rows[1]["split"] = "test" if self.rows[0]["split"] != "test" else "train"
        with self.assertRaisesRegex(ValueError, "leaks"):
            validate_manifest(self.rows)

    def test_episode_label_changes_rejected(self):
        self.rows[1]["label"] = "A"
        self.rows[1]["state"]["variant"] = "occluded"
        with self.assertRaisesRegex(ValueError, "Episode has conflicting"):
            validate_manifest(self.rows)

    def test_duplicate_frame_rejected(self):
        self.rows[1]["frame_index"] = 0
        with self.assertRaisesRegex(ValueError, "Duplicate frame"):
            validate_manifest(self.rows)

    def test_contract_drift_rejected(self):
        self.rows[0]["prompt_sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "contract"):
            validate_manifest(self.rows)

    def test_path_escape_rejected(self):
        for bad in ("../outside.png", "C:/outside.png", "/outside.png", "..\\outside.png"):
            with self.subTest(path=bad):
                rows = copy.deepcopy(self.rows)
                rows[0]["image"] = bad
                with self.assertRaisesRegex(ValueError, "relative PNG"):
                    validate_manifest(rows)

    def test_planned_captures_cannot_train(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaisesRegex(ValueError, "Planned"):
                validate_manifest(self.rows, Path(directory), require_images=True)

    def test_real_images_and_hash_freeze(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = self.capture(root)
            self.assertTrue(validate_manifest(rows, root, require_images=True)["images_checked"])
            rows[0]["image_sha256"] = file_sha256(root / rows[0]["image"])
            (root / rows[0]["image"]).write_bytes(png(99))
            with self.assertRaisesRegex(ValueError, "hash changed"):
                validate_manifest(rows, root, require_images=True)

    def test_identical_image_cross_split_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = self.capture(root)
            other = next(row for row in rows if row["split"] != rows[0]["split"])
            (root / other["image"]).write_bytes((root / rows[0]["image"]).read_bytes())
            with self.assertRaisesRegex(ValueError, "content leaks"):
                validate_manifest(rows, root, require_images=True)

    def test_corrupt_or_wrong_size_png_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = self.capture(root)
            for payload in (png(0, 447, 256), png()[:-7]):
                (root / rows[0]["image"]).write_bytes(payload)
                with self.assertRaises(ValueError):
                    validate_manifest(rows, root, require_images=True)

    def test_portable_records_and_jsonl(self):
        result = training_records(self.rows, self.contract)
        self.assertEqual(result[0]["prompt"][0]["content"][0]["type"], "image")
        self.assertEqual(result[0]["prompt"][0]["content"][1]["text"], self.contract["prompt"])
        self.assertEqual(result[0]["completion"][0]["content"], "G")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "metadata.jsonl"
            write_jsonl(path, result)
            self.assertEqual(read_jsonl(path), result)


if __name__ == "__main__":
    unittest.main()

