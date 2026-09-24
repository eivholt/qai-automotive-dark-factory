import copy
import json
import itertools
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/"scripts/agent"))
import single_token as st


def history(code="C", status="completed"):
    arrival = {"role": "user", "content": json.dumps({"body_id": "private-id", "event": "arrival",
        "arrival": {"status": "completed", "completed_clock_seconds": 3.2, "holder_engaged": False}})}
    action = st.tool_response({"code": code}, {}, "hash")["choices"][0]["message"]
    result = {"role": "tool", "tool_call_id": action["tool_calls"][0]["id"], "content": json.dumps({
        "status": status, "completed_clock_seconds": 43.2, "holder_engaged": status == "completed"})}
    return [arrival, action, result]


def named(h):
    return st.format_messages(st.model_messages(h, "policy"), "named")


class NamedHistoryTests(unittest.TestCase):
    def test_identity_and_timing_variants_have_identical_model_input(self):
        original = history()
        changed = copy.deepcopy(original)
        value = json.loads(changed[0]["content"])
        value["body_id"] = "a-different-body"
        value["arrival"]["completed_clock_seconds"] = 9999.333
        changed[0]["content"] = json.dumps(value)
        value = json.loads(changed[2]["content"])
        value.update(completed_clock_seconds=10099.341, capture_age_at_reply_seconds=3.273)
        changed[2]["content"] = json.dumps(value)
        self.assertEqual(named(original), named(changed))
        self.assertIn("private-id", original[0]["content"])
        self.assertNotIn("private-id", str(named(original)))

    def test_result_explicitly_identifies_job_without_inventing_completion(self):
        for code, job in (("B", "apply_sealant"), ("C", "place_panel"), ("E", "weld_panel")):
            for status in ("completed", "failed", "cancelled_by_backend_switch"):
                result = json.loads(named(history(code, status))[-1]["content"].removeprefix("Tool result: "))
                self.assertEqual(result, {"job": job, "status": status, "holder_engaged": status == "completed",
                                         "completed_jobs": [job] if status == "completed" else []})

    def test_invalid_green_and_reason_are_preserved(self):
        h = history("D")
        value = {"status": "invalid", "stage": "adhesive", "decision": "G", "valid": False,
                 "reason": "stale_reply", "capture_age_at_reply_seconds": 9.25}
        h[-1]["content"] = json.dumps(value)
        result = json.loads(named(h)[-1]["content"].removeprefix("Tool result: "))
        self.assertEqual(result, {"inspection": "adhesive", "completed_jobs": [], **{k:v for k,v in value.items() if k != "capture_age_at_reply_seconds"}})
        self.assertNotIn("allowed_actions", result)

    def test_observation_changes_remain_distinguishable(self):
        h = history("D")
        rendered = []
        for code, valid in (("G", True), ("A", True), ("R", True), ("G", False)):
            h[-1]["content"] = json.dumps({"stage": "adhesive", "decision": code, "valid": valid})
            rendered.append(json.dumps(named(h)))
        self.assertEqual(len(set(rendered)), 4)

    def test_equivalent_json_field_orders_have_identical_tokens_before_tokenization(self):
        h = history("A")
        values = dict(status="observed", stage="panel_presence", decision="R", valid=True, holder_engaged=False)
        variants = set()
        for keys in itertools.permutations(values):
            h[-1]["content"] = json.dumps({key: values[key] for key in keys})
            variants.add(json.dumps(named(h)))
        self.assertEqual(len(variants), 1)

    def test_hidden_truth_and_unpaired_calls_still_fail(self):
        h = history()
        with self.assertRaises(ValueError):
            named(h[:-1])
        h[-1]["content"] = '{"status":"completed","expected_action":"weld_panel"}'
        with self.assertRaises(ValueError):
            named(h)

    def test_all_actions_keep_named_results_and_no_action_filter(self):
        for code in st.ACTIONS:
            rows = named(history(code))
            self.assertEqual(rows[-2]["content"], code)
            self.assertEqual(len(rows), 4)
        self.assertEqual(len(st.ACTIONS), 13)

    def test_examples_preserve_config_policy_and_bind_prompt_identity(self):
        class Backend(st.Controller):
            def post(self, path, body):
                return {"tokens": [100 + ord(body["content"])]}
        config = {"system_prompt": "Policy. Return exactly one tool call with a brief reason, no narrative."}
        base = Backend("http://127.0.0.1", config, observation_format="named")
        example = Backend("http://127.0.0.1", config, observation_format="named", policy_examples="\nExample: facts only.")
        self.assertTrue(example.prompt.startswith(st.make_prompt(config)))
        self.assertIn("Example: facts only.", example.prompt)
        self.assertNotEqual(base.prompt_hash, example.prompt_hash)
        self.assertEqual(base.token_ids, example.token_ids)
        with self.assertRaises(ValueError):
            Backend("http://127.0.0.1", config, prompt="override", policy_examples="example")

    def test_completed_job_ledger_preserves_order_repeats_and_failures(self):
        h = history("B")
        for code, status in (("C", "failed"), ("B", "completed"), ("C", "completed")):
            h.extend(history(code, status)[1:])
        rows = named(h)
        ledger = [json.loads(row["content"].removeprefix("Tool result: "))["completed_jobs"] for row in rows[3::2]]
        self.assertEqual(ledger, [["apply_sealant"], ["apply_sealant"],
                         ["apply_sealant", "apply_sealant"], ["apply_sealant", "apply_sealant", "place_panel"]])


if __name__ == "__main__":
    unittest.main()
