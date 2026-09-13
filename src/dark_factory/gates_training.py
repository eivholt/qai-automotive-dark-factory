"""Deterministic stage/label-balanced minibatches without crossing data splits."""
from collections import defaultdict
import random


class BalancedBatches:
    """One camera layout per batch; thermal G/A/R triplets share an episode.

    A size-one batch preserves the earlier pilot's stage/label round robin.
    The caller supplies training rows only; this class never opens images.
    """
    def __init__(self, rows, batch_size=1, seed=42):
        if not isinstance(batch_size, int) or isinstance(batch_size, bool) or not 1 <= batch_size <= 16:
            raise ValueError("batch_size must be an integer in 1..16")
        if not rows:
            raise ValueError("Training rows cannot be empty")
        self.rows, self.batch_size = rows, batch_size
        self.rng = random.Random(seed)
        self.buckets = defaultdict(lambda: defaultdict(list))
        episodes = defaultdict(lambda: defaultdict(dict))
        for i, row in enumerate(rows):
            stage, label = row["stage"], row["answer"]
            if label not in "GAR" or len(label) != 1:
                raise ValueError("Unknown training label")
            self.buckets[stage][label].append(i)
            if label in episodes[stage][row["episode_id"]]:
                # Multiple observations of a label are fine for ordinary sampling;
                # paired sampling requires an unambiguous triplet.
                episodes[stage][row["episode_id"]][label] = None
            else:
                episodes[stage][row["episode_id"]][label] = i
        self.stages = sorted(self.buckets)
        self.offsets = dict.fromkeys(self.stages, 0)
        self.triplets = {}
        for stage in ("adhesive", "weld"):
            if stage in episodes:
                self.triplets[stage] = [d for d in episodes[stage].values()
                    if set(d) == set("GAR") and all(v is not None for v in d.values())]
                if batch_size >= 3 and not self.triplets[stage]:
                    raise ValueError("Matched thermal batches require complete G/A/R episode triplets")
        self.queues = {}
        self.batch_index = 0

    def _draw(self, key, choices):
        if not self.queues.get(key):
            self.queues[key] = list(choices)
            self.rng.shuffle(self.queues[key])
        return self.queues[key].pop()

    def next(self):
        stage = self.stages[self.batch_index % len(self.stages)]
        self.batch_index += 1
        labels = sorted(self.buckets[stage])
        indices = []
        if self.batch_size >= 3 and stage in self.triplets:
            while len(indices) + 3 <= self.batch_size:
                triplet = self._draw((stage, "triplets"), self.triplets[stage])
                indices.extend(triplet[label] for label in ("G", "A", "R"))
            # Rotate the remainder so batch4 does not overrepresent one label.
        while len(indices) < self.batch_size:
            label = labels[self.offsets[stage] % len(labels)]
            self.offsets[stage] += 1
            indices.append(self._draw((stage, label), self.buckets[stage][label]))
        return [self.rows[i] for i in indices]
