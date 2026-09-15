import unittest
from dark_factory.gates_training import BalancedBatches


def fixture():
    return [dict(stage=s, answer=l, episode_id=str(e), sample_id=f"{s}-{l}-{e}")
        for s in ("panel_presence", "adhesive", "panel_seated", "weld") for e in range(8)
        for l in (("G", "R") if s.startswith("panel_") else ("G", "A", "R"))]


class BalancedTrainingTests(unittest.TestCase):
    def test_batch_four_matched_thermal_and_balanced_panel_stages(self):
        sampler = BalancedBatches(fixture(), 4, 17)
        thermal_remainders = []
        for _ in range(3):
            thermal, presence, seated, weld = [sampler.next() for _ in range(4)]
            for batch in (thermal, weld):
                self.assertEqual(len({r["stage"] for r in batch}), 1)
                self.assertEqual([r["answer"] for r in batch[:3]], list("GAR"))
                self.assertEqual(len({r["episode_id"] for r in batch[:3]}), 1)
            for batch in (presence, seated):
                self.assertEqual([r["answer"] for r in batch], list("GRGR"))
            thermal_remainders.append(thermal[-1]["answer"])
        self.assertEqual(set(thermal_remainders), set("GAR"))

    def test_batch_six_accumulation_four_covers_two_thermal_triplets_per_stage(self):
        rows = fixture()
        sampler = BalancedBatches(rows, batch_size=6, seed=42)
        seen_thermal_episodes = {stage: set() for stage in ("adhesive", "weld")}
        for _ in range(64):
            update = [sampler.next() for _ in range(4)]
            self.assertEqual([batch[0]["stage"] for batch in update],
                ["adhesive", "panel_presence", "panel_seated", "weld"])
            self.assertTrue(all(len(batch) == 6 for batch in update))
            for batch in (update[0], update[3]):
                self.assertEqual([row["answer"] for row in batch], list("GARGAR"))
                self.assertEqual(len({row["stage"] for row in batch}), 1)
                for triplet in (batch[:3], batch[3:]):
                    self.assertEqual(len({row["episode_id"] for row in triplet}), 1)
                    seen_thermal_episodes[triplet[0]["stage"]].add(triplet[0]["episode_id"])
            self.assertTrue(all(row in rows for batch in update for row in batch))
        self.assertEqual(seen_thermal_episodes, {stage: {str(e) for e in range(8)}
            for stage in ("adhesive", "weld")})

    def test_reproducible_and_different_seed(self):
        def series(seed):
            s = BalancedBatches(fixture(), 8, seed)
            return [[r["sample_id"] for r in s.next()] for _ in range(12)]
        self.assertEqual(series(1), series(1))
        self.assertNotEqual(series(1), series(2))

    def test_single_sample_round_robin_and_no_unknown_rows(self):
        rows = fixture()
        s = BalancedBatches(rows, 1)
        batches = [s.next()[0] for _ in range(12)]
        self.assertEqual([r["stage"] for r in batches[:4]], ["adhesive", "panel_presence", "panel_seated", "weld"])
        self.assertEqual([r["answer"] for r in batches[::4]], list("AGR"))
        self.assertTrue(all(r in rows for r in batches))

    def test_incomplete_triplets_fail(self):
        with self.assertRaises(ValueError):
            BalancedBatches([r for r in fixture() if r["answer"] != "A"], 4)
        with self.assertRaises(ValueError):
            BalancedBatches(fixture(), True)
