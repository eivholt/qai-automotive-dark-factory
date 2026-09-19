import contextlib
import copy
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts/agent"))
try:
    import policy_cache as cache
finally:
    sys.path.pop(0)


class Backend:
    endpoint = "http://127.0.0.1:18195"
    prompt = "editable policy"
    prompt_hash = "policy-hash"

    def __init__(self, directory):
        self.directory = directory
        self.calls = []

    def post(self, path, body):
        self.calls.append((path, body))
        if path == "/apply-template":
            return {"prompt": self.prompt + "[USER]" + body["messages"][1]["content"]}
        if path == "/completion":
            # Even a hazardous-looking setup token must never become a tool call.
            return {"tokens_predicted": 1, "tokens": [1073], "content": "I"}
        if path.endswith("save"):
            (self.directory / body["filename"]).write_bytes(b"computed KV, not a decision")
            return {"n_saved": 25}
        if path.endswith("restore"):
            return {"n_restored": 25}
        raise AssertionError(path)


class PolicyCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.identity = {"schema": "darkfactory-native-cache-identity-v1",
                         "model_path": "/private/model.gguf", "model_sha256": "weights",
                         "runtime_sha256": {"llama-server": "runtime"}, "arguments": ["--cache-type-k", "f16"]}
        self.write_identity()
        self.backend = Backend(self.directory)
        self.props = patch.object(cache, "urlopen", side_effect=lambda *a, **k:
            contextlib.closing(io.StringIO(json.dumps({"model_path": "/private/model.gguf"}))))
        self.props.start()
        self.addCleanup(self.props.stop)

    def write_identity(self):
        (self.directory / "identity.json").write_text(json.dumps(self.identity))

    def test_restore_survives_new_controller_without_reusing_a_decision(self):
        first = cache.prepare(self.backend, self.directory)
        self.assertEqual(first["source"], "evaluated")
        self.assertEqual(first["discarded_setup_tokens"], 1)
        self.assertFalse(first["decision_reused"])
        fresh = Backend(self.directory)
        restored = cache.prepare(fresh, self.directory)
        self.assertEqual(restored["source"], "restored")
        self.assertFalse(restored["decision_reused"])
        self.assertFalse(any(path == "/completion" for path, _ in fresh.calls))

    def test_policy_template_model_runtime_and_settings_invalidate(self):
        original = copy.deepcopy(self.identity)
        for field, replacement in (("model_sha256", "new weights"),
                                   ("runtime_sha256", {"llama-server": "new runtime"}),
                                   ("arguments", ["--cache-type-k", "q8_0"])):
            self.identity = copy.deepcopy(original); self.write_identity()
            cache.prepare(self.backend, self.directory)
            self.identity[field] = replacement; self.write_identity()
            self.assertEqual(cache.prepare(self.backend, self.directory)["source"], "evaluated")
        self.backend.prompt_hash = "changed-policy"
        self.assertEqual(cache.prepare(self.backend, self.directory)["source"], "evaluated")
        self.backend.prompt += " changed rendered template"
        self.assertEqual(cache.prepare(self.backend, self.directory)["source"], "evaluated")

    def test_corrupt_cache_and_manifest_are_recomputed(self):
        cache.prepare(self.backend, self.directory)
        (self.directory / "policy.bin").write_bytes(b"corrupt")
        self.assertEqual(cache.prepare(self.backend, self.directory)["source"], "evaluated")
        for malformed in ("{", "null", "[]", '"wrong shape"'):
            (self.directory / "policy.json").write_text(malformed)
            self.assertEqual(cache.prepare(self.backend, self.directory)["source"], "evaluated")

    def test_wrong_running_model_fails_before_evaluation_or_restore(self):
        self.identity["model_path"] = "/another/model.gguf"; self.write_identity()
        with self.assertRaisesRegex(ValueError, "running model"):
            cache.prepare(self.backend, self.directory)
        self.assertEqual(self.backend.calls, [])


if __name__ == "__main__":
    unittest.main()
