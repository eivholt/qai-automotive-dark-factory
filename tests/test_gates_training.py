import unittest
from dark_factory.gates_training import BalancedBatches


def fixture():
    return [dict(stage=s, answer=l, episode_id=str(e), sample_id=f"{s}-{l}-{e}")
        for s in ("doors", "adhesive", "weld") for e in range(8)
        for l in (("G", "R") if s == "doors" else ("G", "A", "R"))]


class BalancedTrainingTests(unittest.TestCase):
    def test_batch_four_matched_thermal_and_balanced_doors(self):
        sampler = BalancedBatches(fixture(), 4, 17)
        thermal_remainders = []
        for _ in range(3):
            thermal, doors, weld = sampler.next(), sampler.next(), sampler.next()
            for batch in (thermal, weld):
                self.assertEqual(len({r["stage"] for r in batch}), 1)
                self.assertEqual([r["answer"] for r in batch[:3]], list("GAR"))
                self.assertEqual(len({r["episode_id"] for r in batch[:3]}), 1)
            self.assertEqual([r["answer"] for r in doors], list("GRGR"))
            thermal_remainders.append(thermal[-1]["answer"])
        self.assertEqual(set(thermal_remainders), set("GAR"))

    def test_reproducible_and_different_seed(self):
        def series(seed):
            s = BalancedBatches(fixture(), 8, seed)
            return [[r["sample_id"] for r in s.next()] for _ in range(9)]
        self.assertEqual(series(1), series(1))
        self.assertNotEqual(series(1), series(2))

    def test_single_sample_round_robin_and_no_unknown_rows(self):
        rows = fixture()
        s = BalancedBatches(rows, 1)
        batches = [s.next()[0] for _ in range(9)]
        self.assertEqual([r["stage"] for r in batches[:3]], ["adhesive", "doors", "weld"])
        self.assertEqual([r["answer"] for r in batches[::3]], list("AGR"))
        self.assertTrue(all(r in rows for r in batches))

    def test_incomplete_triplets_fail(self):
        with self.assertRaises(ValueError):
            BalancedBatches([r for r in fixture() if r["answer"] != "A"], 4)
        with self.assertRaises(ValueError):
            BalancedBatches(fixture(), True)
