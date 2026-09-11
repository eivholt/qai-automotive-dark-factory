"""Deterministic capture plans and checks; no ML dependencies or invented images."""
from __future__ import annotations

import copy
import hashlib
import json
import math
import random
from collections import Counter, defaultdict
from pathlib import Path, PurePosixPath
from typing import Any

from .protocol import load_contract, prompt_sha256, validate_png

LABELS = ("G", "A", "R")
SPLITS = ("train", "validation", "test")


def canonical_hash(value: Any) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False).encode()).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_jsonl(path: Path) -> list[dict]:
    rows = []
    with path.open(encoding="utf-8-sig") as source:
        for number, line in enumerate(source, 1):
            if line.strip():
                value = json.loads(line)
                if not isinstance(value, dict):
                    raise ValueError(f"{path}:{number}: expected an object")
                rows.append(value)
    return rows


def write_jsonl(path: Path, rows: list[dict]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="\n") as target:
        for row in rows:
            target.write(json.dumps(row, sort_keys=True, allow_nan=False) + "\n")


def split_groups(group_ids: list[str], fractions: dict[str, float], seed: int) -> dict[str, str]:
    """Split complete nuisance families, never adjacent frames or matched labels."""
    if len(set(group_ids)) != len(group_ids) or not group_ids:
        raise ValueError("Group IDs must be nonempty and unique")
    if set(fractions) != set(SPLITS) or any(not math.isfinite(v) or v <= 0 for v in fractions.values()):
        raise ValueError("All three split fractions must be finite and positive")
    if not math.isclose(sum(fractions.values()), 1.0):
        raise ValueError("Split fractions must sum to one")
    if len(group_ids) < 3:
        raise ValueError("At least three groups are needed for independent splits")
    # Hamilton apportionment is exact and independent of caller ordering.
    counts = {split: int(len(group_ids) * fractions[split]) for split in SPLITS}
    remainder = len(group_ids) - sum(counts.values())
    order = sorted(SPLITS, key=lambda s: (-(len(group_ids) * fractions[s] - counts[s]), SPLITS.index(s)))
    for split in order[:remainder]:
        counts[split] += 1
    if not all(counts.values()):
        raise ValueError("Increase groups so each split receives at least one group")
    shuffled = sorted(group_ids)
    random.Random(seed).shuffle(shuffled)
    result = {}
    cursor = 0
    for split in SPLITS:
        for group in shuffled[cursor:cursor + counts[split]]:
            result[group] = split
        cursor += counts[split]
    return result


def _uniform(rng: random.Random, bounds: list[float]) -> float:
    if len(bounds) != 2 or not all(math.isfinite(v) for v in bounds) or bounds[0] > bounds[1]:
        raise ValueError(f"Invalid randomization range: {bounds}")
    return round(rng.uniform(*bounds), 6)


def generate_manifest(config: dict, contract: dict | None = None) -> list[dict]:
    """Plan captures. Each G/A/R triplet shares nuisance draws exactly."""
    contract = contract or load_contract()
    groups, frames, seed = config["groups"], config["frames_per_episode"], config["seed"]
    if not isinstance(groups, int) or groups < 3 or not isinstance(frames, int) or frames < 1:
        raise ValueError("groups >= 3 and frames_per_episode >= 1 are required")
    if (config["sensor"]["width"], config["sensor"]["height"]) != (contract["image"]["width"], contract["image"]["height"]):
        raise ValueError("Capture size must match the inspection contract")
    if config["sensor"]["mode"] != "active_nir_proxy":
        raise ValueError("Balanced G/A/R plans require active illumination; capture sensor-off as an A-only stress panel")
    fingerprint = canonical_hash(config)[:12]
    ids = [f"{fingerprint}-group-{index:06d}" for index in range(groups)]
    assignments = split_groups(ids, config["split_fractions"], seed)
    result = []
    for index, group in enumerate(ids):
        group_seed = int(hashlib.sha256(f"{seed}:{index}".encode()).hexdigest()[:8], 16) & 0x7fffffff
        nuisance_rng = random.Random(group_seed)
        state_rng = random.Random(group_seed ^ 0x51A7E)
        sensor = {key: _uniform(nuisance_rng, value) if isinstance(value, list) else value for key, value in config["sensor"].items()}
        randomization = {}
        for key, bounds in config["randomization"].items():
            count = 2 if key == "tray_offset_cm" else 3 if "jitter" in key else 0
            randomization[key] = [_uniform(nuisance_rng, bounds) for _ in range(count)] if count else _uniform(nuisance_rng, bounds)
        target_module = state_rng.randrange(4)
        red_variant = "missing" if index % 2 == 0 else "misaligned"
        for label, variant in (("G", "normal"), ("A", "occluded"), ("R", red_variant)):
            episode = f"{group}-{label}"
            state = {"variant": variant, "module_index": target_module, "misalignment_deg": _uniform(state_rng, config["state"]["misalignment_deg"]) if variant == "misaligned" else 0.0, "occlusion_fraction": _uniform(state_rng, config["state"]["occlusion_fraction"]) if label == "A" else 0.0}
            for frame in range(frames):
                sample = f"{episode}-{frame:04d}"
                result.append({"schema_version": 1, "capture_status": "planned", "scenario": config["scenario"], "sample_id": sample, "episode_id": episode, "leakage_group": group, "frame_index": frame, "split": assignments[group], "image": f"images/{sample}.png", "label": label, "seed": group_seed, "contract_version": contract["version"], "prompt_sha256": prompt_sha256(contract), "randomization_config_sha256": canonical_hash(config), "sensor": copy.deepcopy(sensor), "randomization": copy.deepcopy(randomization), "state": state.copy()})
    return result


def resolve_image(root: Path, relative: str) -> Path:
    normalized = relative.replace("\\", "/")
    parts = PurePosixPath(normalized)
    if parts.is_absolute() or ".." in parts.parts or ":" in normalized or not normalized.lower().endswith(".png"):
        raise ValueError(f"Image must be a relative PNG inside the dataset: {relative}")
    candidate = (root / normalized).resolve()
    if not candidate.is_relative_to(root.resolve()):
        raise ValueError("Image path escapes dataset root")
    return candidate


def validate_manifest(rows: list[dict], root: Path | None = None, contract: dict | None = None, require_images: bool = False, require_balanced: bool = True) -> dict:
    contract = contract or load_contract()
    if not rows:
        raise ValueError("Manifest is empty")
    samples, image_paths = set(), set()
    group_splits, episode_groups, episode_splits, episode_labels = {}, {}, {}, {}
    episode_frames = set()
    group_counts = defaultdict(Counter)
    group_nuisance = {}
    hashes: dict[str, tuple[str, str]] = {}
    counts: dict[str, Counter] = {split: Counter() for split in SPLITS}
    expected_size = (contract["image"]["width"], contract["image"]["height"])
    for row in rows:
        for key in ("sample_id", "episode_id", "leakage_group", "split", "image", "label", "contract_version", "prompt_sha256", "frame_index", "seed", "sensor", "randomization", "state"):
            if key not in row:
                raise ValueError(f"Missing field {key}")
        sample, group, episode, split, label = (row[key] for key in ("sample_id", "leakage_group", "episode_id", "split", "label"))
        if not all(isinstance(value, str) and value for value in (sample, group, episode)):
            raise ValueError("Sample, episode and leakage group IDs must be nonempty strings")
        if sample in samples or row["image"] in image_paths:
            raise ValueError("Duplicate sample ID or image path")
        samples.add(sample)
        image_paths.add(row["image"])
        if split not in SPLITS or label not in LABELS:
            raise ValueError("Unknown split or label")
        if row["contract_version"] != contract["version"] or row["prompt_sha256"] != prompt_sha256(contract):
            raise ValueError("Dataset prompt/contract does not match runtime")
        if not isinstance(row["frame_index"], int) or row["frame_index"] < 0:
            raise ValueError("frame_index must be a nonnegative integer")
        if group_splits.setdefault(group, split) != split or episode_splits.setdefault(episode, split) != split:
            raise ValueError("Episode/nuisance family leaks across splits")
        if episode_groups.setdefault(episode, group) != group:
            raise ValueError("Episode appears in multiple leakage groups")
        if episode_labels.setdefault(episode, label) != label:
            raise ValueError("Episode has conflicting labels")
        frame_key = (episode, row["frame_index"])
        if frame_key in episode_frames:
            raise ValueError("Duplicate frame index in one episode")
        episode_frames.add(frame_key)
        nuisance = canonical_hash({"sensor": row["sensor"], "randomization": row["randomization"]})
        if group_nuisance.setdefault(group, nuisance) != nuisance:
            raise ValueError("Matched group has label-dependent or changing nuisance metadata")
        if (row["sensor"].get("width"), row["sensor"].get("height")) != expected_size:
            raise ValueError("Sensor dimensions do not match contract")
        variant = row["state"].get("variant")
        expected_label = {"normal": "G", "occluded": "A", "missing": "R", "misaligned": "R", "sensor_off": "A"}.get(variant)
        if label != expected_label:
            raise ValueError("Variant conflicts with label")
        path = resolve_image(root or Path.cwd(), row["image"])
        if require_images:
            if row.get("capture_status") == "planned":
                raise ValueError("Planned rows must be replaced by verified capture metadata before training")
            validate_png(path.read_bytes(), width=expected_size[0], height=expected_size[1])
            digest = file_sha256(path)
            if row.get("image_sha256") and row["image_sha256"] != digest:
                raise ValueError("Image hash changed after dataset freeze")
            prior = hashes.setdefault(digest, (split, label))
            if prior[0] != split:
                raise ValueError("Identical image content leaks across splits")
            if prior[1] != label:
                raise ValueError("Identical image content has conflicting labels")
        counts[split][label] += 1
        group_counts[group][label] += 1
    if require_balanced:
        for group, count in group_counts.items():
            if set(count) != set(LABELS) or len(set(count.values())) != 1:
                raise ValueError(f"Matched group {group} must contain balanced G/A/R: {dict(count)}")
        for split, count in counts.items():
            if set(count) != set(LABELS) or len(set(count.values())) != 1:
                raise ValueError(f"{split} must contain balanced G/A/R counts: {dict(count)}")
    return {"rows": len(rows), "groups": len(group_splits), "episodes": len(episode_groups), "counts": {key: dict(value) for key, value in counts.items()}, "images_checked": require_images, "unique_image_hashes": len(hashes), "manifest_sha256": canonical_hash(rows), "prompt_sha256": prompt_sha256(contract)}


def training_records(rows: list[dict], contract: dict | None = None) -> list[dict]:
    """Portable image-first, user-only, one-letter completion format."""
    contract = contract or load_contract()
    return [{"sample_id": row["sample_id"], "episode_id": row["episode_id"], "leakage_group": row["leakage_group"], "split": row["split"], "image": row["image"], "prompt": [{"role": "user", "content": [{"type": "image", "image": row["image"]}, {"type": "text", "text": contract["prompt"]}]}], "completion": [{"role": "assistant", "content": row["label"]}]} for row in rows]

