"""Probes preserve evidence boundaries; these tests do not establish LLM accuracy."""
import copy
import importlib.util
import json
from pathlib import Path
import sys
import unittest

agent_dir = Path(__file__).resolve().parents[1] / "scripts/agent"
sys.path.insert(0, str(agent_dir))
try:
    spec = importlib.util.spec_from_file_location("departure_probes", agent_dir / "probe_departure.py")
    probes = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(probes)
finally:
    sys.path.pop(0)


class DepartureProbeTests(unittest.TestCase):
    def fixture(self):
        messages = [{"role": "user", "content": json.dumps({"body_id": "original", "arrival": {
            "status": "completed", "completed_clock_seconds": 3.2}})}]
        for index, stage in enumerate(("panel_presence", "adhesive")):
            messages += [{"role": "assistant", "tool_calls": [{"id": str(index), "type": "function",
                "function": {"name": "inspect", "arguments": json.dumps({"stage": stage, "reason": "inspect"})}}]},
                {"role": "tool", "tool_call_id": str(index), "content": json.dumps({"stage": stage,
                 "decision": "G", "valid": True, "completed_clock_seconds": 10+index*10})}]
        return {"source_preassembled": True, "audits": [{"hidden_temperature": 25}],
                "agent": {"current_body_messages": messages}}

    def test_variants_keep_missing_weld_evidence_and_do_not_send_audit(self):
        runtime = self.fixture()
        original = copy.deepcopy(runtime)
        variants = list(probes.cases(runtime, 2))
        self.assertEqual(runtime, original)
        self.assertEqual(len(variants), 4)
        for _, messages in variants:
            wire = json.dumps(messages)
            self.assertNotIn("hidden_temperature", wire)
            self.assertNotIn("source_preassembled", wire)
            results = [json.loads(m["content"]) for m in messages if m["role"] == "tool"]
            self.assertNotIn("weld", [r["stage"] for r in results])
            self.assertEqual(results[-1]["decision"], "G")
            self.assertEqual(results[-1]["stage"], "adhesive")
        retry = variants[1][1]
        results = [json.loads(m["content"]) for m in retry if m["role"] == "tool"]
        self.assertEqual([r["decision"] for r in results], ["G", "A", "G"])
        self.assertLess(results[-2]["completed_clock_seconds"], results[-1]["completed_clock_seconds"])
        ids = [m["tool_calls"][0]["id"] for m in retry if m["role"] == "assistant"]
        self.assertEqual(len(ids), len(set(ids)))
        self.assertEqual(ids, [m["tool_call_id"] for m in retry if m["role"] == "tool"])

    def test_missing_panel_fixture_rejected(self):
        runtime = self.fixture()
        runtime["agent"]["current_body_messages"][2]["content"] = json.dumps({"stage": "panel_presence", "decision": "R", "valid": True})
        with self.assertRaises(ValueError):
            list(probes.cases(runtime, 1))


if __name__ == "__main__":
    unittest.main()
