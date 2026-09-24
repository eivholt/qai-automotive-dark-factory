import copy
import itertools
import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"scripts/agent"))
import single_token as st
from probe_simple_policy import history, temperatures, cases


class SimpleToolsTests(unittest.TestCase):
    def render(self, rows):
        return st.format_messages(st.model_messages(rows, "policy", st.SIMPLE_ACTIONS), "named", st.SIMPLE_ACTIONS)

    def test_each_choice_round_trips_without_generated_parameters(self):
        config = json.loads((Path(__file__).resolve().parents[1]/"configs/live_agent.json").read_text())
        self.assertEqual({t["function"]["name"] for t in config["tools"]}, {v[0] for v in st.actions_for(config).values()})
        for code in st.SIMPLE_ACTIONS:
            message = st.tool_response(dict(code=code), {}, "hash", st.SIMPLE_ACTIONS)["choices"][0]["message"]
            self.assertEqual(st.action_code(message, st.SIMPLE_ACTIONS), code)
            self.assertEqual(set(json.loads(message["tool_calls"][0]["function"]["arguments"])), {"reason"})
            if code != "H":  # hold has the same no-parameter signature in v1.
                with self.assertRaises(ValueError):
                    st.action_code(message)

    def test_nested_thermal_facts_are_canonical_and_identity_free(self):
        value = temperatures("R", "G")
        value["results"][1].update(valid=False, reason="stale_reply", body_id="secret", capture_age_at_reply_seconds=999)
        original = self.render(history([("C", value)]))
        for keys in itertools.permutations(value):
            changed = {key: value[key] for key in keys}
            changed["results"] = [dict(reversed(list(r.items()))) for r in value["results"]]
            self.assertEqual(original, self.render(history([("C", changed)])))
        text = original[-1]["content"]
        self.assertNotIn("secret", text)
        self.assertNotIn("999", text)
        result = json.loads(text.removeprefix("Tool result: "))
        self.assertEqual(result["results"][0]["decision"], "R")
        self.assertFalse(result["results"][1]["valid"])
        self.assertEqual(result["results"][1]["reason"], "stale_reply")

    def test_partial_prepare_is_not_a_completed_job(self):
        value = dict(status="partial", completed_steps=["apply_sealant"])
        actual = json.loads(self.render(history([("B", value)]))[-1]["content"].removeprefix("Tool result: "))
        self.assertEqual(actual["completed_jobs"], [])
        self.assertEqual(actual["completed_steps"], ["apply_sealant"])
        self.assertEqual(actual["job"], "prepare_panel")

    def test_cancelled_scan_does_not_invent_second_result(self):
        value = dict(status="cancelled_by_backend_switch", results=temperatures()["results"][:1])
        actual = json.loads(self.render(history([("C", value)]))[-1]["content"].removeprefix("Tool result: "))
        self.assertEqual(len(actual["results"]), 1)
        self.assertEqual(actual["status"], "cancelled_by_backend_switch")

    def test_nested_source_truth_or_unbounded_steps_are_rejected(self):
        for value in (dict(results=[dict(expected_action="finish_body")]),
                      dict(completed_steps=["invented"]), dict(results=[{}, {}, {}]),
                      dict(results=[dict(results=[])])):
            with self.assertRaises(ValueError):
                self.render(history([("C", value)]))

    def test_probe_expectations_never_enter_model_inputs(self):
        names = []
        for name, rows, expected in cases():
            names.append(name)
            self.assertTrue(set(expected) <= set(st.SIMPLE_ACTIONS))
            rendered = self.render(rows)
            self.assertNotIn('"expected_choices"', json.dumps(rendered))
        self.assertEqual(len(names), len(set(names)))


if __name__ == "__main__":
    unittest.main()
