"""Conservative G/A/R decisions from unmasked top-K probability bounds."""
import math
from .gates import LABELS
from .protocol import ProtocolError

SCORING_METHOD = "gar-unmasked-topk-intervals-v1"


def topk_label_bounds(candidates, expected_count=20):
    """Bound omitted classes by the smallest returned top-K probability.

    This assumes the backend's documented complete, unmasked top-K list.
    It never renormalizes away omitted vocabulary mass or forces G/A/R logits.
    Bounds describe the probabilities supplied by the floating-point backend.
    """
    if not isinstance(candidates, list) or len(candidates) != expected_count:
        raise ProtocolError("Incomplete top-K probability response")
    probabilities = []
    known = {}
    for item in candidates:
        logp = item.get("logprob")
        if isinstance(logp, bool) or not isinstance(logp, (int,float)) or not math.isfinite(logp) or logp > 1e-6:
            raise ProtocolError("Invalid unmasked log probability")
        probability = min(1.0, math.exp(logp))
        probabilities.append(probability)
        token = item.get("token")
        if token in LABELS:
            if token in known:
                raise ProtocolError("Duplicate class token in top-K response")
            known[token] = probability
    if sum(probabilities) > 1.00001:
        raise ProtocolError("Top-K probabilities exceed total vocabulary mass")
    cutoff = max(min(probabilities), float.fromhex("0x1.0p-1022"))
    missing = [label for label in LABELS if label not in known]
    lower_mass = min(1.0, sum(known.values()))
    upper_mass = min(1.0, lower_mass + len(missing) * cutoff)
    lower = {label: known.get(label, 0.0) / upper_mass if upper_mass else 0.0 for label in LABELS}
    upper = {label: min(1.0, known[label] / lower_mass) if label in known and lower_mass else
        (cutoff / (lower_mass + cutoff) if label not in known else 1.0) for label in LABELS}
    return {"scoring_method": SCORING_METHOD, "topk_count": len(candidates), "topk_cutoff_probability": cutoff,
        "observed_label_probabilities": known, "missing_labels": missing,
        "label_mass_lower_bound": lower_mass, "label_mass_upper_bound": upper_mass,
        "conditional_label_lower_bounds": lower, "conditional_label_upper_bounds": upper}


def decision_from_bounds(bounds, policy):
    lower = bounds["conditional_label_lower_bounds"]
    if bounds["label_mass_lower_bound"] < policy["minimum_label_mass"]:
        return "A", "insufficient_label_support"
    winner = max(LABELS, key=lambda label: lower[label])
    if lower[winner] < policy["minimum_conditional_probability"]:
        return "A", "ambiguous_label_scores"
    return winner, "model_classification_bounded" if bounds["missing_labels"] else "model_classification"
