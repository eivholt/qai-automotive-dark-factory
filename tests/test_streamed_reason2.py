"""Evaluation guards; device feature/KV parity is checked on the actual EVK."""
import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts/model"))
spec = importlib.util.spec_from_file_location("stream_probe", ROOT / "scripts/model/probe_streamed_reason2.py")
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)


class StreamedReason2Tests(unittest.TestCase):
    def test_pair_prompt_does_not_silently_relax_thermal(self):
        contract = probe.load_gate_contract()
        for stage in ("adhesive", "weld"):
            with self.assertRaises(ValueError):
                probe.nir_pair_prompt(dict(stage=stage, **contract["stages"][stage]))
        for stage in ("panel_presence", "panel_seated"):
            text = probe.nir_pair_prompt(dict(stage=stage, **contract["stages"][stage]))
            self.assertIn("in order spot1, spot2.", text)
            self.assertNotIn("spot3", text)

    def test_branch_errors_are_not_hidden_by_accuracy(self):
        rows = []
        for number, (expected, decision) in enumerate((("G", "R"), ("R", "G"), ("A", "R"), ("A", "A"))):
            result = {"decision": decision, "label_probabilities": {"G": .3, "A": .3, "R": .4}}
            rows.append(dict(sample_id=str(number), stage="panel_presence", expected=expected,
                             baseline=result, candidate=result))
        summary = probe.summarize({"mode": "nir-pair", "rows": rows})["panel_presence"]
        self.assertEqual(summary["same_decisions"], 4)
        self.assertEqual(summary["candidate"], {"correct": 1, "false_green": 1, "wrong_presence_red": 2})
        with self.assertRaises(ValueError):
            probe.summarize({"mode": "nir-pair", "rows": rows + rows[:1]})

    def test_incomplete_or_duplicate_timing_pairs_are_rejected(self):
        rows = [dict(stage="weld", trial=i, mode=mode, elapsed_ms=100,
                     result={"label_probabilities": {"G": 1}})
                for i in range(6) for mode in ("baseline", "streamed")]
        self.assertEqual(probe.summarize({"mode": "schedule", "rows": rows})["weld"]["exact_probability_pairs"], 5)
        with self.assertRaises(ValueError):
            probe.summarize({"mode": "schedule", "rows": rows[:-1]})
        rows[-1]["trial"] = 1
        with self.assertRaises(ValueError):
            probe.summarize({"mode": "schedule", "rows": rows})


if __name__ == "__main__":
    unittest.main()
