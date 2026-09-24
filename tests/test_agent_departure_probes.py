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

    def test_legacy_score_denominator_is_unchanged(self):
        rows = list(probes.cases(self.fixture(), 8))
        self.assertEqual(len(rows), 16)
        self.assertEqual([name for name, _ in rows],
                         [f"id{variant}-retry{retry}" for variant in range(8) for retry in (0, 1)])

    def test_thermal_order_suite_covers_both_missing_stages_and_completed_orders(self):
        rows = {name: (messages, allowed) for name, messages, allowed in probes.thermal_order_cases(self.fixture())}
        self.assertEqual(len(rows), 17)
        self.assertEqual(rows["missing-weld"][1], (("inspect", "weld"),))
        self.assertEqual(rows["missing-sealant"][1], (("inspect", "adhesive"),))
        for name in ("both-adhesive-then-weld", "both-weld-then-adhesive", "adhesive-recovered", "weld-recovered"):
            self.assertEqual(rows[name][1], (("act", "send_forward"),))
        results = [json.loads(m["content"]) for m in rows["missing-sealant"][0] if m["role"] == "tool"]
        self.assertEqual([(result["stage"], result["decision"], result["valid"]) for result in results],
                         [("panel_presence", "G", True), ("weld", "G", True)])

    def test_thermal_order_observations_are_paired_private_and_do_not_mutate_fixture(self):
        runtime = self.fixture()
        original = copy.deepcopy(runtime)
        prefix = probes.compact_messages(runtime["agent"]["current_body_messages"][:3])
        for name, messages, allowed in probes.thermal_order_cases(runtime):
            self.assertEqual(messages[:3], prefix)
            wire = json.dumps(messages)
            for excluded in ("hidden_temperature", "source_preassembled", "allowed_choices", name):
                self.assertNotIn(excluded, wire)
            ids = [m["tool_calls"][0]["id"] for m in messages if m["role"] == "assistant"]
            self.assertEqual(len(ids), len(set(ids)))
            self.assertEqual(ids, [m["tool_call_id"] for m in messages if m["role"] == "tool"])
            results = [json.loads(m["content"]) for m in messages if m["role"] == "tool"]
            clocks = [result["completed_clock_seconds"] for result in results]
            self.assertEqual(clocks, sorted(set(clocks)))
            valid_cool_stages = {result["stage"] for result in results
                                if result["decision"] == "G" and result["valid"]}
            self.assertEqual(("act", "send_forward") in allowed,
                             {"adhesive", "weld"} <= valid_cool_stages)
        self.assertEqual(runtime, original)

    def test_invalid_thermal_evidence_never_allows_departure_or_wrong_stage(self):
        rows = {name: (messages, allowed) for name, messages, allowed in probes.thermal_order_cases(self.fixture())}
        for stage, other in (("adhesive", "weld"), ("weld", "adhesive")):
            for suffix in ("ambiguous", "invalid-green", "stale-green", "hot", f"stale-before-{other}"):
                messages, allowed = rows[f"{stage}-{suffix}"]
                self.assertEqual(set(allowed), {("inspect", stage), ("wait", 1), ("wait", 5), ("wait", 15), ("hold", None)})
                evidence = [json.loads(m["content"]) for m in messages if m["role"] == "tool"
                            and json.loads(m["content"])["stage"] == stage]
                self.assertEqual(len(evidence), 1)
                self.assertFalse(evidence[0]["decision"] == "G" and evidence[0]["valid"])
                if suffix.startswith("stale"):
                    self.assertFalse(evidence[0]["valid"])
                    self.assertEqual(evidence[0]["reason"], "stale_reply")
                    self.assertGreater(evidence[0]["capture_age_at_reply_seconds"], 8)

    def test_thermal_order_requires_valid_preassembled_presence(self):
        for decision, valid in (("R", True), ("A", True), ("G", False)):
            runtime = self.fixture()
            runtime["agent"]["current_body_messages"][2]["content"] = json.dumps({
                "stage": "panel_presence", "decision": decision, "valid": valid})
            with self.assertRaises(ValueError):
                list(probes.thermal_order_cases(runtime))


if __name__ == "__main__":
    unittest.main()
