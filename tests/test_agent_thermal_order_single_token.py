"""Exact offline gate probes must not pass an inspection of the wrong stage."""
from pathlib import Path
import sys
import unittest

agent_dir = Path(__file__).resolve().parents[1] / "scripts/agent"
sys.path.insert(0, str(agent_dir))
try:
    from probe_single_token import choice_matches
finally:
    sys.path.pop(0)


class ThermalOrderChoiceTests(unittest.TestCase):
    def test_exact_choices_reject_wrong_stage_and_departure(self):
        allowed = (("inspect", "adhesive"), ("wait", 1), ("hold", None))
        for selected in (["inspect", "adhesive"], ["wait", 1], ["hold", None]):
            self.assertTrue(choice_matches(selected, allowed_choices=allowed))
        for selected in (["inspect", "weld"], ["act", "send_forward"], ["wait", 7], None):
            self.assertFalse(choice_matches(selected, allowed_choices=allowed))

    def test_legacy_fault_tool_and_exact_expected_behavior_stays_separate(self):
        self.assertTrue(choice_matches(["inspect", "weld"], allowed_tools={"inspect", "wait", "hold"}))
        self.assertFalse(choice_matches(["act", "send_forward"], allowed_tools={"inspect", "wait", "hold"}))
        self.assertTrue(choice_matches(["inspect", "weld"], ["inspect", "weld"]))
        self.assertFalse(choice_matches(["inspect", "adhesive"], ["inspect", "weld"]))


if __name__ == "__main__":
    unittest.main()
