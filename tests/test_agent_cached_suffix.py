"""Offline benchmark provenance and preparation boundaries, not LLM accuracy."""
import contextlib
import copy
import io
import json
import math
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

agent_dir = Path(__file__).resolve().parents[1] / "scripts/agent"
sys.path.insert(0, str(agent_dir))
try:
    import benchmark_cached_suffix as bench
    import single_token as st
finally:
    sys.path.pop(0)


class Backend(st.Controller):
    def __init__(self):
        self.requests = []
        self.winner = 1065
        super().__init__("http://127.0.0.1:18208", {}, prompt="policy", observation_format="json")

    def post(self, path, body):
        self.requests.append((path, copy.deepcopy(body)))
        if path == "/tokenize":
            return {"tokens": [1065 + ord(body["content"]) - ord("A")]}
        if path == "/apply-template":
            return {"prompt": "".join(f'<{message["role"]}>{message["content"]}' for message in body["messages"]) + "<assistant>"}
        if body["n_predict"] == 0:
            # Pinned servers may emit a token while warming. Never score it.
            return {"tokens": [9999], "tokens_predicted": 1, "timings": {"prompt_ms": 12}}
        return {"content": "A", "tokens": [self.winner], "tokens_predicted": 1,
                "generation_settings": {"post_sampling_probs": False, "grammar": "", "logit_bias": [],
                                        "samplers": ["temperature"], "temperature": 0},
                "completion_probabilities": [{"top_logprobs": [
                    {"id": self.winner, "logprob": math.log(.6)}, {"id": 1066, "logprob": math.log(.3)}]}],
                "timings": {"prompt_n": 61, "prompt_ms": 100, "predicted_n": 1}}


def history():
    arrival = {"role": "user", "content": json.dumps({"body_id": "body-id", "event": "arrival", "arrival": {
        "status": "completed", "completed_clock_seconds": 5, "holder_engaged": False}})}
    action = st.tool_response({"code": "A"}, {}, "hash")["choices"][0]["message"]
    result = {"role": "tool", "tool_call_id": action["tool_calls"][0]["id"], "content": json.dumps({
        "status": "observed", "stage": "panel_presence", "decision": "G", "valid": True,
        "reason": "LAST_UNSEEN_RESULT"})}
    return [arrival, action, result]


def document(messages, prompt="policy"):
    return {"version": st.VERSION, "prompt_sha256": bench.sha256(prompt.encode()),
            "diagnostic": {"model_messages": messages, "response": {"content": "must not enter model"}},
            "source_truth": "must not enter model"}


class CachedSuffixTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.backend = Backend()
        self.messages = st.model_messages(history(), self.backend.prompt)

    def write(self, value, name="request-001.json"):
        path = self.directory / name
        path.write_text(json.dumps(value), encoding="utf-8")
        return path

    def test_source_and_message_hashes_preserve_only_saved_model_visible_input(self):
        source = self.write(document(self.messages))
        records = bench.load_records(self.directory, self.backend.prompt)
        self.assertEqual(len(records), 1)
        self.assertEqual(records[0]["source_sha256"], bench.sha256(source.read_bytes()))
        self.assertEqual(records[0]["messages"], self.messages)
        self.assertEqual(records[0]["messages_sha256"], bench.sha256(json.dumps(self.messages, separators=(",", ":")).encode()))
        self.assertNotIn("source_truth", records[0])
        self.assertNotIn("response", records[0])

    def test_mismatched_prompt_and_provenance_are_rejected(self):
        for change in ("header", "system", "version"):
            value = document(copy.deepcopy(self.messages))
            if change == "header":
                value["prompt_sha256"] = "wrong"
            elif change == "system":
                value["diagnostic"]["model_messages"][0]["content"] = "different policy"
            else:
                value["version"] = "old-contract"
            self.write(value)
            with self.assertRaises(ValueError):
                bench.load_records(self.directory, self.backend.prompt)

    def test_input_count_size_roles_and_hidden_fields_are_bounded(self):
        self.write(document(self.messages))
        self.write(document(self.messages), "request-002.json")
        with patch.object(bench, "MAX_LOGS", 1), self.assertRaises(ValueError):
            bench.load_records(self.directory, self.backend.prompt)
        with patch.object(bench, "MAX_LOG_BYTES", 1), self.assertRaises(ValueError):
            bench.load_records(self.directory, self.backend.prompt)
        variants = [self.messages[:-1], self.messages + [{"role": "assistant", "content": "A"}]]
        hidden = copy.deepcopy(self.messages)
        hidden[-1]["content"] = 'Tool result: {"source_temperature": 25}'
        variants.append(hidden)
        for messages in variants:
            with self.assertRaises(ValueError):
                bench.validate_messages(messages, self.backend.prompt)

    def test_preparation_excludes_unknown_arrival_or_latest_observation(self):
        kind, prefix = bench.known_prefix(self.backend, self.messages[:2])
        self.assertEqual(kind, "policy")
        self.assertNotIn("body-id", prefix)
        kind, prefix = bench.known_prefix(self.backend, self.messages)
        self.assertEqual(kind, "tool-envelope")
        self.assertIn("body-id", prefix)
        self.assertTrue(prefix.endswith("<assistant>A<user>Tool result: "))
        self.assertNotIn("LAST_UNSEEN_RESULT", prefix)
        self.assertNotIn("__DF_PENDING_TOOL", prefix)

    def test_benchmark_uses_identical_completion_request_and_discards_warm_token(self):
        self.backend.decide(history())
        ordinary = [body for path, body in self.backend.requests if path == "/completion"][-1]
        self.backend.requests.clear()
        self.write(document(self.messages))
        record = bench.load_records(self.directory, self.backend.prompt)[0]
        row = bench.run_record(self.backend, record, 0)
        requests = [body for path, body in self.backend.requests if path == "/completion"]
        self.assertEqual(len(requests), 2)
        self.assertEqual(requests[-1], ordinary)
        self.assertNotIn("LAST_UNSEEN_RESULT", requests[0]["prompt"])
        self.assertIn("LAST_UNSEEN_RESULT", requests[1]["prompt"])
        self.assertEqual(row["discarded_prep_tokens"], 1)
        self.assertEqual(row["tokens"], [1065])
        self.assertEqual(row["score"]["code"], "A")
        self.assertEqual(row["source_sha256"], record["source_sha256"])
        self.assertEqual(row["rendered_sha256"], bench.sha256(ordinary["prompt"].encode()))
        self.assertEqual(row["timings"]["prompt_n"], 61)
        self.assertNotIn("error", row)

    def test_invalid_unmasked_winner_is_recorded_without_dispatch_or_repair(self):
        self.write(document(self.messages))
        record = bench.load_records(self.directory, self.backend.prompt)[0]
        self.backend.winner = 9999
        self.backend.requests.clear()
        row = bench.run_record(self.backend, record, 0)
        self.assertIn("unmasked", row["error"])
        self.assertNotIn("score", row)
        self.assertEqual(row["tokens"], [9999])
        self.assertEqual({path for path, _ in self.backend.requests}, {"/apply-template", "/completion"})

    def test_pass_bounds_fail_before_backend_access(self):
        for passes in (0, 6):
            args = ["benchmark", "--logs", "unused", "--endpoint", "unused", "--output", "unused", "--passes", str(passes)]
            with patch.object(sys, "argv", args), patch.object(bench, "Controller") as controller, \
                    contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
                bench.main()
            self.assertEqual(error.exception.code, 2)
            controller.assert_not_called()


if __name__ == "__main__":
    unittest.main()
