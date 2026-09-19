import copy
import importlib.util
import json
import math
from pathlib import Path
import sys
import unittest


agent_dir = Path(__file__).resolve().parents[1] / "scripts/agent"
sys.path.insert(0, str(agent_dir))
try:
    import single_token as st
    import serve_single_token as server
finally:
    sys.path.pop(0)


def raw(winner=1065, other=1066):
    return {"tokens_predicted": 1, "tokens": [winner], "generation_settings": {
            "post_sampling_probs": False, "grammar": "", "logit_bias": [], "samplers": ["temperature"], "temperature": 0},
            "completion_probabilities": [{"top_logprobs": [
                {"id": winner, "logprob": math.log(.6)}, {"id": other, "logprob": math.log(.3)}]}]}


def arrival():
    return {"role": "user", "content": json.dumps({"body_id": "new-body", "event": "arrival",
            "arrival": {"status": "completed", "holder_engaged": False}})}


class SingleTokenTests(unittest.TestCase):
    def test_experimental_rows_round_trip_without_filling_missing_evidence(self):
        for value in ({"status": "completed", "holder_engaged": False, "completed_clock_seconds": 1.125},
                      {"status": "observed", "stage": "weld", "valid": False, "decision": "G", "reason": "stale_reply"}):
            self.assertEqual(st.decode_row(st.observation_row(value)), value)
        with self.assertRaises(ValueError):
            st.observation_row({"valid": None})
        with self.assertRaises(ValueError):
            st.decode_row([None]*7 + [{"valid": True}])

    def test_experimental_formats_do_not_change_the_default_policy(self):
        self.assertEqual(st.format_prompt("policy", "json"), "policy")
        original = st.model_messages([arrival()], "policy")
        self.assertEqual(st.format_messages(original, "json"), original)
        for mode in ("rows", "aliases"):
            changed = st.format_messages(original, mode)
            self.assertEqual(json.loads(changed[1]["content"])["body_id"], "new-body")
            self.assertIsInstance(json.loads(original[1]["content"])["arrival"], dict)

    def test_all_actions_translate_without_recipe_filter(self):
        for code, (tool, key, value) in st.ACTIONS.items():
            reply = st.tool_response({"code": code}, {}, "hash")
            message = reply["choices"][0]["message"]
            self.assertEqual(st.action_code(message), code)
            self.assertEqual(message["tool_calls"][0]["function"]["name"], tool)
            self.assertIn("no generated explanation", message["tool_calls"][0]["function"]["arguments"])
        self.assertEqual(len(st.ACTIONS), 13)

    def test_topk_scores_keep_mass_bounds_for_omitted_labels(self):
        result = st.score_response(raw(), {"A": 1065, "B": 1066, "C": 1067})
        self.assertEqual(result["code"], "A")
        self.assertAlmostEqual(result["winning_probability"], .6)
        self.assertAlmostEqual(result["label_mass_lower"], .9)
        self.assertAlmostEqual(result["label_mass_upper"], 1.)
        self.assertIsNone(result["label_probabilities"]["C"])

    def test_non_action_winner_is_not_forced_into_menu(self):
        with self.assertRaises(ValueError):
            st.score_response(raw(9999, 1065), {"A": 1065, "B": 1066})

    def test_biased_masked_or_invalid_scores_are_rejected(self):
        samples = []
        for key, value in (("post_sampling_probs", True), ("grammar", "forced"), ("logit_bias", [[1065, 100]]),
                           ("samplers", []), ("temperature", 1), ("lora", [{"id": 0, "scale": 1}])):
            sample = raw(); sample["generation_settings"][key] = value; samples.append(sample)
        sample = raw(); sample["completion_probabilities"][0]["top_logprobs"][0]["logprob"] = float("nan"); samples.append(sample)
        sample = raw(); sample["tokens_predicted"] = 2; samples.append(sample)
        sample = raw(); sample["tokens"] = [1066]; samples.append(sample)
        for sample in samples:
            with self.assertRaises(ValueError):
                st.score_response(sample, {"A": 1065, "B": 1066})

    def test_actual_history_keeps_invalid_observation_and_call_pairing(self):
        assistant = st.tool_response({"code": "D"}, {}, "hash")["choices"][0]["message"]
        tool = {"role": "tool", "tool_call_id": assistant["tool_calls"][0]["id"],
                "content": '{"stage":"adhesive","decision":"A","valid":false,"reason":"stale_reply"}'}
        history = [arrival(), assistant, tool]
        messages = st.model_messages(history, "policy")
        self.assertEqual(messages[2], {"role": "assistant", "content": "D"})
        self.assertIn('"valid":false', messages[3]["content"])
        self.assertIn("stale_reply", messages[3]["content"])
        with self.assertRaises(ValueError):
            st.model_messages(history[:-1], "policy")
        tool["tool_call_id"] = "wrong-id"
        with self.assertRaises(ValueError):
            st.model_messages(history, "policy")

    def test_hidden_source_fields_are_rejected(self):
        with self.assertRaises(ValueError):
            st.observation('{"decision":"G","expected_decision":"G"}')
        message = arrival()
        content = json.loads(message["content"]); content["seed"] = 123
        message["content"] = json.dumps(content)
        with self.assertRaises(ValueError):
            st.model_messages([message], "policy")

    def test_old_variable_wait_is_not_silently_changed(self):
        message = st.tool_response({"code": "K"}, {}, "hash")["choices"][0]["message"]
        message["tool_calls"][0]["function"]["arguments"] = '{"seconds":7,"reason":"wait"}'
        with self.assertRaises(ValueError):
            st.action_code(message)

    def test_proxy_rejects_different_prompt_or_tools(self):
        config = {"system_prompt": "policy", "tools": []}
        payload = {"messages": [{"role": "system", "content": "policy"}, arrival()], "tools": [],
                   "model": "dark-factory-agent", "tool_choice": "required", "parallel_tool_calls": False}
        self.assertEqual(server.validate_payload(payload, config), [arrival()])
        payload["messages"][0]["content"] = "different"
        with self.assertRaises(ValueError):
            server.validate_payload(payload, config)
        for malformed in (None, [], {"messages": "text"}):
            with self.assertRaises(ValueError):
                server.validate_payload(malformed, config)

    def test_tokenizer_must_verify_single_unique_codes(self):
        class BadTokenizer(st.Controller):
            def post(self, path, body):
                return {"tokens": [1, 2]}
        with self.assertRaises(ValueError):
            BadTokenizer("http://127.0.0.1", {}, prompt="policy")
        class DuplicateTokenizer(st.Controller):
            def post(self, path, body):
                return {"tokens": [1]}
        with self.assertRaises(ValueError):
            DuplicateTokenizer("http://127.0.0.1", {}, prompt="policy")


if __name__ == "__main__":
    unittest.main()
