"""The cache benchmark must never attach saved evidence to a different job."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch


spec = importlib.util.spec_from_file_location("cache_replay", Path(__file__).resolve().parents[1] / "scripts/agent/replay_latency.py")
replay = importlib.util.module_from_spec(spec)
spec.loader.exec_module(replay)


def assistant(tool, arguments, call_id):
    return {"role": "assistant", "content": "", "tool_calls": [{"id": call_id, "type": "function",
            "function": {"name": tool, "arguments": json.dumps(arguments)}}]}


class CacheReplayTests(unittest.TestCase):
    def run_probe(self, mismatch=False):
        original = assistant("inspect", {"stage": "panel_presence", "reason": "Observe panel"}, "old-id")
        following = assistant("act", {"action": "apply_sealant", "reason": "Panel missing"}, "old-next")
        runtime = {"agent": {"system_prompt": "Choose tools", "tools": [], "current_body_messages": [
            {"role": "user", "content": "Arrived"}, original,
            {"role": "tool", "tool_call_id": "old-id", "content": '{"stage":"panel_presence","decision":"R","valid":true}'},
            following]}}
        generated = assistant("hold", {"reason": "Needs attention"}, "new-id") if mismatch else assistant(
            "inspect", {"stage": "panel_presence", "reason": "Observe incoming panel"}, "new-id")
        responses = [generated, following]
        requests = []

        def endpoint(request, timeout):
            requests.append(json.loads(request.data))
            response = {"choices": [{"message": responses[len(requests)-1]}]}
            return io.BytesIO(json.dumps(response).encode())

        with tempfile.TemporaryDirectory() as directory:
            source, output = Path(directory) / "runtime.json", Path(directory) / "report.json"
            source.write_text(json.dumps(runtime))
            argv = ["replay_latency", "--runtime", str(source), "--output", str(output), "--continue-generated"]
            with patch("sys.argv", argv), patch.object(replay, "urlopen", endpoint), contextlib.redirect_stdout(io.StringIO()):
                result = replay.main()
        return result, requests, generated

    def test_choice_mismatch_stops_before_attaching_saved_result(self):
        result, requests, _ = self.run_probe(mismatch=True)
        self.assertEqual(result, 1)
        self.assertEqual(len(requests), 1)
        self.assertFalse(any(m["role"] == "tool" for m in requests[0]["messages"]))

    def test_matching_reply_preserves_arguments_and_remaps_result_id(self):
        result, requests, generated = self.run_probe()
        self.assertEqual(result, 0)
        self.assertEqual(len(requests), 2)
        self.assertEqual(requests[1]["messages"][2], generated)
        self.assertEqual(requests[1]["messages"][3]["tool_call_id"], "new-id")


if __name__ == "__main__":
    unittest.main()
