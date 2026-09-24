import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("placement", Path(__file__).resolve().parents[1] /
                                            "scripts/agent/prepare_genie_device.py")
placement = importlib.util.module_from_spec(spec)
spec.loader.exec_module(placement)


class DevicePlacementTests(unittest.TestCase):
    def test_changes_only_device_and_file_references_without_modifying_bundle(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in ("tokenizer.json", "model.bin"):
                (root / name).write_bytes(b"private source fixture")
            extensions = {"devices": [{"device_id": 0, "soc_model": 77, "dsp_arch": "v73",
                                      "cores": [{"core_id": 0, "perf_profile": "burst"}]}],
                          "graphs": [{"num_cores": 1}], "context": [{"weight_sharing_enabled": True}]}
            config = {"dialog": {"sampler": {"temp": 0.8}, "context": {"size": 4096},
                "tokenizer": {"path": "tokenizer.json"}, "engine": {
                    "backend": {"type": "QnnHtp", "extensions": "backend.json"},
                    "model": {"type": "binary", "binary": {"ctx-bins": ["model.bin"]}}}}}
            inventory = {"schema": "qnn-platform-inventory-v1", "devices": [
                {"device_id": i, "soc_model": 77, "arch": 73, "cores": [{"core_id": 0}]} for i in (0, 1)]}
            for name, data in (("genie.json", config), ("backend.json", extensions), ("platform.json", inventory)):
                (root / name).write_text(json.dumps(data))
            before = {p.name: p.read_bytes() for p in root.iterdir()}
            path = placement.prepare(root / "genie.json", root / "platform.json", 1, root / "candidate")
            result = json.loads(path.read_text())
            new_ext = json.loads((path.parent / "backend_extensions.json").read_text())
            extensions["devices"][0]["device_id"] = 1
            self.assertEqual(new_ext, extensions)
            self.assertEqual(result["dialog"]["sampler"], config["dialog"]["sampler"])
            self.assertEqual(result["dialog"]["context"], config["dialog"]["context"])
            self.assertEqual(result["dialog"]["engine"]["model"]["binary"]["ctx-bins"], [str(root / "model.bin")])
            self.assertEqual(result["dialog"]["tokenizer"]["path"], str(root / "tokenizer.json"))
            self.assertEqual(before, {name: (root / name).read_bytes() for name in before})
            with self.assertRaises(FileExistsError):
                placement.prepare(root / "genie.json", root / "platform.json", 0, path.parent)
            with self.assertRaisesRegex(ValueError, "not uniquely present"):
                placement.prepare(root / "genie.json", root / "platform.json", 2, root / "bad-device")
            inventory["devices"][1]["cores"] = [{"core_id": 1}]
            (root / "platform.json").write_text(json.dumps(inventory))
            with self.assertRaisesRegex(ValueError, "core selection"):
                placement.prepare(root / "genie.json", root / "platform.json", 1, root / "bad-core")
            self.assertFalse((root / "bad-device").exists())
            self.assertFalse((root / "bad-core").exists())
            inventory["devices"][1]["cores"] = [{"core_id": 0}]
            inventory["devices"][1]["arch"] = 75
            (root / "platform.json").write_text(json.dumps(inventory))
            with self.assertRaisesRegex(ValueError, "architecture"):
                placement.prepare(root / "genie.json", root / "platform.json", 1, root / "bad-arch")
            self.assertFalse((root / "bad-arch").exists())


if __name__ == "__main__":
    unittest.main()
