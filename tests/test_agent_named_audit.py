import copy
import hashlib
import json
import math
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
import verify_single_token_agent as audit


class NamedAuditTests(unittest.TestCase):
    def fixture(self):
        prompt = audit.format_prompt("policy", "named")
        digest = hashlib.sha256(prompt.encode()).hexdigest()
        tokens = {code: i+100 for i, code in enumerate(audit.ACTIONS)}
        health = dict(version=audit.VERSION, observation_format="named", history_version=audit.HISTORY_VERSION,
                      prompt_sha256=digest, token_ids=tokens)
        results = [dict(status="completed", holder_engaged=False, completed_clock_seconds=2.345),
                   dict(status="observed", stage="panel_presence", decision="R", valid=True,
                        completed_clock_seconds=5.678, capture_age_at_reply_seconds=0.222)]
        trace, logs, prior = [], [], []
        for i, code in enumerate(("A", "B")):
            trace.append(dict(event="agent_observation", body_id="body-1", detail=dict(tool_results=results[:i+1])))
            tool, key, value = audit.ACTIONS[code]
            trace.append(dict(event="agent_choice", body_id="body-1", detail=dict(tool=tool, argument=value,
                              reason=f"Selected action code {code}; no generated explanation")))
            messages = [dict(role="system", content=prompt), dict(role="user", content=json.dumps(dict(
                body_id="body-1", event="new_body_arrived_choose_tools_to_complete_the_task", arrival=results[0])))]
            for previous, result in zip(prior, results[1:]):
                messages += [dict(role="assistant", content=previous), dict(role="user", content="Tool result: "+json.dumps(result))]
            raw = dict(tokens_predicted=1, tokens=[tokens[code]], generation_settings=dict(samplers=["temperature"], temperature=0),
                completion_probabilities=[dict(top_logprobs=[dict(id=tokens[code], logprob=math.log(.8))])])
            logs.append(dict(prompt_sha256=digest, request_body_id="body-1", diagnostic=dict(response=raw,
                model_messages=audit.format_messages(messages,"named")), response=dict(usage=dict(completion_tokens=1), single_token=dict(code=code))))
            prior.append(code)
        return dict(agent=dict(trace=trace)), logs, health

    def run_audit(self, logs=None, health=None):
        runtime, good_logs, good_health = self.fixture()
        with patch.object(audit, "verify_live", return_value={"errors": []}):
            return audit.verify(runtime, good_logs if logs is None else logs,
                                good_health if health is None else health, {}, "policy")

    def test_actual_named_history_matches(self):
        self.assertTrue(self.run_audit()["passed"])

    def test_body_affinity_and_actual_observations_cannot_be_forged(self):
        _, original, _ = self.fixture()
        for kind in ("body", "completion", "decision", "action", "order"):
            logs = copy.deepcopy(original)
            if kind == "body":
                logs[1]["request_body_id"] = "body-2"
            elif kind == "action":
                logs[1]["diagnostic"]["model_messages"][2]["content"] = "C"
            else:
                row = logs[1]["diagnostic"]["model_messages"][-1]
                value = json.loads(row["content"].removeprefix("Tool result: "))
                if kind == "order":
                    value = dict(reversed(list(value.items())))
                else:
                    value["completed_jobs" if kind == "completion" else "decision"] = ["place_panel"] if kind == "completion" else "G"
                row["content"] = "Tool result: " + json.dumps(value, separators=(",", ":"))
            self.assertFalse(self.run_audit(logs)["passed"], kind)

    def test_named_version_is_checked(self):
        _, _, health = self.fixture()
        health["history_version"] = "unreviewed"
        self.assertFalse(self.run_audit(health=health)["passed"])


if __name__ == "__main__":
    unittest.main()
