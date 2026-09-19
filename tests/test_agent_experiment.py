"""Offline experiment interlocks, not a validation of the production executor."""
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("agent_experiment", Path(__file__).resolve().parents[1] / "scripts/agent/experiment.py")
experiment = importlib.util.module_from_spec(spec)
spec.loader.exec_module(experiment)


class AgentExperimentTests(unittest.TestCase):
    def cell(self, name="missing"):
        return experiment.ReplayCell(next(stream for case, stream, _ in experiment.fixtures() if case == name))

    def presence(self, cell):
        cell.inspect("panel_presence")

    def test_one_presence_batch_authorizes_assembly(self):
        cell = self.cell()
        cell.inspect("panel_presence")
        cell.act("apply_sealant")
        self.assertEqual(cell.completed, ["apply_sealant"])

    def test_inspection_tool_collects_exactly_one_batch(self):
        cell = self.cell()
        result = cell.call("inspect", {"stage": "panel_presence"})
        self.assertEqual(len(result["batches"]), 1)
        self.assertEqual(cell.branch, "missing")

    def test_cooling_wait_is_observable_without_granting_permission(self):
        cell = self.cell("thermal_waits")
        self.presence(cell)
        cell.act("apply_sealant")
        cell.act("place_panel")
        result = cell.call("inspect", {"stage": "adhesive"})
        self.assertTrue(result["state"]["cooling_wait_required"])
        self.assertNotIn("adhesive", result["state"]["permits"])
        cell.call("wait", {})
        self.assertFalse(cell.status()["cooling_wait_required"])
        with self.assertRaises(ValueError):
            cell.act("spot_weld")
        self.assertEqual(len(cell.seen), 2)  # One presence batch plus the hot thermal batch.

    def test_presence_branch_commits_after_first_valid_batch(self):
        cell = self.cell("presence_first_batch_commits")
        cell.call("inspect", {"stage": "panel_presence"})
        self.assertEqual(cell.branch, "missing")
        with self.assertRaises(ValueError):
            cell.call("inspect", {"stage": "panel_presence"})
        self.assertEqual(cell.indices["panel_presence"], 1)

    def test_preassembled_cannot_assemble_and_needs_both_thermal_gates(self):
        cell = self.cell("preassembled")
        self.presence(cell)
        with self.assertRaises(ValueError):
            cell.act("apply_sealant")
        with self.assertRaises(ValueError):
            cell.act("depart")
        for stage in ("adhesive", "weld"):
            cell.inspect(stage)
        cell.act("depart")
        self.assertEqual(cell.completed, ["depart"])

    def test_missing_sequence_and_command_idempotency(self):
        cell = self.cell()
        self.presence(cell)
        cell.act("apply_sealant")
        cell.act("apply_sealant")
        cell.act("place_panel")
        with self.assertRaises(ValueError):
            cell.act("spot_weld")
        cell.inspect("adhesive")
        cell.act("spot_weld")
        with self.assertRaises(ValueError):
            cell.act("release_panel")
        for stage in ("panel_seated", "weld"):
            cell.inspect(stage)
        cell.act("release_panel")
        cell.act("depart")
        self.assertEqual(cell.completed, list(experiment.ACTIONS))

    def test_hot_wait_does_not_grant_a_permit(self):
        cell = self.cell("thermal_waits")
        self.presence(cell)
        cell.act("apply_sealant")
        cell.act("place_panel")
        cell.inspect("adhesive")
        cell.call("wait", {})
        with self.assertRaises(ValueError):
            cell.act("spot_weld")
        self.assertNotIn("adhesive", cell.permits)

    def test_stale_error_ambiguous_and_duplicate_evidence_hold(self):
        for name in ("stale", "unavailable"):
            cell = self.cell(name)
            cell.inspect("panel_presence")
            cell.call("wait", {})
            with self.assertRaises(ValueError):
                cell.act("depart")
            self.assertTrue(cell.exception)
        cell = self.cell()
        reply = cell.inspect("panel_presence")["inspection"]
        self.assertFalse(cell.accept("panel_presence", reply)["accepted"])
        self.assertTrue(cell.exception)
        self.assertNotIn("panel_presence", cell.permits)
        with self.assertRaises(ValueError):
            cell.act("apply_sealant")
        cell = self.cell("ambiguous")
        self.presence(cell)
        cell.act("apply_sealant")
        cell.act("place_panel")
        cell.inspect("adhesive")
        self.assertTrue(cell.exception)

    def test_other_epoch_or_body_cannot_grant_permit(self):
        for field, value in (("epoch", 99), ("body_id", "previous-body"), ("stage", "weld")):
            cell = self.cell()
            reply = cell.inspect("panel_presence")["inspection"]
            reply.update(request_id="new-id")
            reply[field] = value
            self.assertFalse(cell.accept("panel_presence", reply)["accepted"])
            self.assertTrue(cell.exception)

    def test_tool_cannot_inject_evidence_or_override_hold(self):
        cell = self.cell()
        for name, args in (("act", {"action": "depart", "force": True}),
                           ("accept", {"decision": "G"}), ("hold", {"decision": "G"})):
            with self.assertRaises(ValueError):
                cell.call(name, args)
        cell.call("hold", {})
        with self.assertRaises(ValueError):
            cell.call("act", {"action": "depart"})


class ExpandedAgentExperimentTests(unittest.TestCase):
    def cell(self, name):
        return experiment.ExpandedReplayCell(next(stream for case, stream, _ in experiment.expanded_fixtures() if case == name))

    def prepare_sealant(self, cell):
        cell.call("inspect", {"stage": "panel_presence"})
        cell.call("inspect", {"stage": "frame_presence"})
        cell.act("apply_sealant")

    def test_frame_installation_requires_no_sealant_but_both_post_weld_checks(self):
        cell = self.cell("frame_missing")
        with self.assertRaises(ValueError):
            cell.inspect("frame_presence")
        cell.call("inspect", {"stage": "panel_presence"})
        with self.assertRaises(ValueError):
            cell.act("apply_sealant")
        cell.call("inspect", {"stage": "frame_presence"})
        cell.act("place_frame")
        cell.act("weld_frame")
        with self.assertRaises(ValueError):
            cell.act("release_frame")
        for stage in ("frame_seated", "frame_weld"):
            cell.call("inspect", {"stage": stage})
        cell.act("release_frame")
        self.assertTrue(cell.frame_ready())
        self.assertNotIn("apply_sealant", cell.completed)

    def test_gap_blocks_placement_and_continuity_does_not_imply_coolness(self):
        cell = self.cell("sealant_gap")
        self.prepare_sealant(cell)
        cell.call("inspect", {"stage": "sealant_coverage"})
        cell.call("wait", {})
        with self.assertRaises(ValueError):
            cell.act("place_panel")
        cell.act("top_up_sealant")
        self.assertNotIn("sealant_coverage", cell.permits)
        cell.call("inspect", {"stage": "sealant_coverage"})
        cell.act("place_panel")
        with self.assertRaises(ValueError):
            cell.act("spot_weld")
        self.assertEqual(cell.top_ups, 1)

    def test_persistent_gaps_cannot_trigger_unbounded_reapplication(self):
        cell = self.cell("persistent_gap")
        self.prepare_sealant(cell)
        for _ in range(2):
            cell.call("inspect", {"stage": "sealant_coverage"})
            cell.act("top_up_sealant")
        cell.call("inspect", {"stage": "sealant_coverage"})
        self.assertTrue(cell.exception)
        with self.assertRaises(ValueError):
            cell.act("top_up_sealant")
        with self.assertRaises(ValueError):
            cell.act("place_panel")

    def test_preassembled_skips_sealant_coverage_and_frame_repair(self):
        cell = self.cell("preassembled")
        cell.call("inspect", {"stage": "panel_presence"})
        with self.assertRaises(ValueError):
            cell.inspect("frame_presence")
        for stage in ("adhesive", "weld"):
            cell.call("inspect", {"stage": stage})
        cell.act("depart")
        self.assertEqual(cell.completed, ["depart"])


if __name__ == "__main__":
    unittest.main()
