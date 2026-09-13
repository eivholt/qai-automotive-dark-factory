"""Separate image-only contract for the automotive process; no legacy prompt reuse."""
from __future__ import annotations
import base64
import binascii
from dataclasses import dataclass
import hashlib
import json
import math
import sys
from pathlib import Path
from typing import Any
from .protocol import ProtocolError, MAX_PNG_BYTES, valid_identifier, validate_png

MAX_GATE_REQUEST_BYTES = 5 * 1024 * 1024
IDENTITY_FIELDS = ("contract_version", "request_id", "body_id", "stage", "captured_at_ms")
LABELS = ("G", "A", "R")
MESSAGE_LAYOUT = "independent-camera-images-newline-v1"

def camera_content(images: list[dict], prompt: str) -> list[dict]:
    """Prevent Qwen-VL temporal merging of adjacent camera image markers."""
    content = []
    for item in images:
        if content:
            content.append({"type": "text", "text": "\n"})
        content.append(item)
    content.append({"type": "text", "text": prompt})
    return content



def load_gate_contract(path: str | Path | None = None) -> dict:
    if path is None:
        candidates = [Path(__file__).resolve().parents[2] / "configs/process_inspection.json", Path(sys.prefix) / "share/qai-automotive-dark-factory/process_inspection.json"]
        path = next((candidate for candidate in candidates if candidate.is_file()), candidates[0])
    path = Path(path)
    c = json.loads(path.read_text(encoding="utf-8"))
    if c.get("version") != "automotive-process-v1" or set(c.get("stages", {})) != {"doors", "adhesive", "weld"}:
        raise ProtocolError("Unsupported process contract")
    if c.get("image") != {"format": "PNG", "width": 448, "height": 256, "bit_depth": 8, "channels": "grayscale", "visual_tokens_per_image": 112}:
        raise ProtocolError("Unsupported process image contract")
    if c.get("model_family") != "nvidia/Cosmos-Reason2-2B":
        raise ProtocolError("Unsupported process model family")
    generation = {"method": "single_next_token_logits", "temperature": 0, "enable_thinking": False, "image_cache_entries": 0}
    if c.get("generation") != generation or c["generation"]["enable_thinking"] is not False or isinstance(c["generation"]["temperature"], bool) or type(c["generation"]["image_cache_entries"]) is not int:
        raise ProtocolError("Unsupported process generation contract")
    if set(c.get("labels", {})) != set(LABELS):
        raise ProtocolError("Process labels must be G, A, R")
    if c.get("thermal_window_c") != [20.0, 120.0]:
        raise ProtocolError("Thermal window must match the fixed sensor")
    for name, stage in c["stages"].items():
        if stage.get("prompt_sha256") != hashlib.sha256(stage["prompt"].encode()).hexdigest():
            raise ProtocolError("Process prompt hash mismatch")
        ids = ["left", "right"] if name == "doors" else ["spot1", "spot2", "spot3"]
        if stage.get("camera_ids") != ids or stage.get("modality") != ("nir" if name == "doors" else "thermal"):
            raise ProtocolError("Process camera ordering mismatch")
        if name != "doors" and (stage.get("ambiguity_band_c") != 3.0 or "3 C ambiguity band" not in stage["prompt"]):
            raise ProtocolError("Process v1 thermal gates require the explicit 3 C ambiguity band")
        if name != "doors" and stage.get("ready_below_c") != {"adhesive": 45.0, "weld": 55.0}[name]:
            raise ProtocolError("Process v1 thermal readiness thresholds are frozen at 45/55 C")
        if name != "doors" and f'{stage["ready_below_c"]:g} C' not in stage["prompt"]:
            raise ProtocolError("Thermal threshold and prompt differ")
    policy = c.get("decision_policy", {})
    if set(policy) != {"minimum_label_mass", "minimum_conditional_probability", "consecutive_green", "max_reply_age_seconds"}:
        raise ProtocolError("Unexpected process decision policy")
    for k in ("minimum_label_mass", "minimum_conditional_probability"):
        value = policy[k]
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or not .5 <= value <= 1:
            raise ProtocolError("Invalid classifier abstention setting")
    if type(policy["consecutive_green"]) is not int or policy["consecutive_green"] != 2:
        raise ProtocolError("Process v1 requires two fresh green observations")
    age = policy["max_reply_age_seconds"]
    if isinstance(age, bool) or not isinstance(age, (int, float)) or not math.isfinite(age) or not 1 <= age <= 60:
        raise ProtocolError("Process reply age must be finite and between 1 and 60 seconds")
    return c


def model_export_alias(provenance: dict) -> str:
    """Bind the backend identity to the exact text/vision pair, including adapters."""
    hashes = [provenance["files"][name]["sha256"] for name in ("text_bf16", "vision_f16")]
    if any(not isinstance(h, str) or len(h) != 64 or any(c not in "0123456789abcdef" for c in h) for h in hashes):
        raise ProtocolError("Invalid export content hash")
    prefix = "Cosmos-Reason2-2B-Automotive-LoRA-BF16" if provenance.get("adapter") else "Cosmos-Reason2-2B-Automotive-Base-BF16"
    return prefix + "-" + "-".join(h[:16] for h in hashes)


def validate_adapter_training(report: dict, contract: dict, base_config_sha256: str | None = None):
    """Reject legacy adapters before loading their tensors into a process gate."""
    expected = {"contract_version": contract["version"], "message_layout": MESSAGE_LAYOUT,
        "prompt_hashes": {k: v["prompt_sha256"] for k, v in contract["stages"].items()},
        "image_contract": contract["image"], "thermal_window_c": contract["thermal_window_c"]}
    if not isinstance(report, dict) or any(report.get(k) != v for k, v in expected.items()):
        raise ProtocolError("Automotive adapter training contract does not match the runtime")
    if base_config_sha256 is not None and report.get("base_model_config_sha256") != base_config_sha256:
        raise ProtocolError("Adapter base model configuration does not match")
    return report


@dataclass(frozen=True)
class GateRequest:
    identity: dict
    camera_ids: tuple[str, ...]
    pngs: tuple[bytes, ...]


def parse_gate_request(payload: Any, contract: dict) -> GateRequest:
    if not isinstance(payload, dict) or set(payload) != {*IDENTITY_FIELDS, "images"}:
        raise ProtocolError("Expected process identity fields and images only")
    if payload["contract_version"] != contract["version"]:
        raise ProtocolError("Process contract version mismatch")
    for k in ("request_id", "body_id"):
        if not valid_identifier(payload[k]):
            raise ProtocolError(f"Invalid {k}")
    stage = payload["stage"]
    if not isinstance(stage, str) or stage not in contract["stages"]:
        raise ProtocolError("Unknown process stage")
    timestamp = payload["captured_at_ms"]
    if isinstance(timestamp, bool) or not isinstance(timestamp, (int, float)) or not math.isfinite(timestamp) or timestamp < 0:
        raise ProtocolError("Invalid capture timestamp")
    expected = contract["stages"][stage]["camera_ids"]
    images = payload["images"]
    if not isinstance(images, list) or len(images) != len(expected):
        raise ProtocolError("Incomplete inspection camera batch")
    pngs = []
    for image, camera_id in zip(images, expected):
        if not isinstance(image, dict) or set(image) != {"camera_id", "png_base64"} or image["camera_id"] != camera_id:
            raise ProtocolError("Camera batch order mismatch")
        encoded = image["png_base64"]
        if not isinstance(encoded, str) or len(encoded) > (MAX_PNG_BYTES + 2) // 3 * 4:
            raise ProtocolError("Oversize camera PNG")
        try:
            png = base64.b64decode(encoded, validate=True)
        except (binascii.Error, ValueError) as exc:
            raise ProtocolError("Invalid camera PNG base64") from exc
        validate_png(png)
        pngs.append(png)
    return GateRequest({k: payload[k] for k in IDENTITY_FIELDS}, tuple(expected), tuple(pngs))


def hold_reply(payload: Any, contract: dict, reason: str, *, valid: bool = False) -> dict:
    payload = payload if isinstance(payload, dict) else {}
    stage = contract["stages"].get(payload.get("stage"), {}) if isinstance(payload.get("stage"), str) else {}
    return {**{k: payload.get(k) for k in IDENTITY_FIELDS}, "decision": "A", "valid": valid,
            "reason": reason, "prompt_sha256": stage.get("prompt_sha256", ""),
            "camera_ids": stage.get("camera_ids", []), "modality": stage.get("modality", ""),
            "source": "host_gpu", "model": "not_loaded", "latency_ms": 0.0,
            "decision_policy": dict(contract["decision_policy"]), "message_layout": MESSAGE_LAYOUT}


def select_decision(scores: dict[str, float], label_mass: float, policy: dict) -> tuple[str, str]:
    if set(scores) != set(LABELS) or any(not math.isfinite(x) or x < 0 for x in scores.values()) or not math.isfinite(label_mass):
        return "A", "invalid_scores"
    if abs(sum(scores.values()) - 1.0) > 1e-4 or not 0 <= label_mass <= 1:
        return "A", "invalid_scores"
    # Restricted-label softmax is not a calibrated probability of correctness.
    winner = max(LABELS, key=lambda label: scores[label])
    if label_mass < policy["minimum_label_mass"]:
        return "A", "insufficient_label_support"
    if scores[winner] < policy["minimum_conditional_probability"]:
        return "A", "ambiguous_label_scores"
    return winner, "model_classification"


class GateLatch:
    """Client-side replay/staleness protection and independent fresh-frame consensus."""
    def __init__(self, contract: dict):
        self.contract = contract
        self.key = None
        self.green_count = 0
        self.last_request = None
        self.last_capture = -1.0

    def observe(self, reply: dict, expected: dict, now_ms: float) -> str:
        key = (expected.get("body_id"), expected.get("stage"))
        if key != self.key:
            self.key, self.green_count, self.last_request, self.last_capture = key, 0, None, -1.0
        valid = isinstance(reply, dict) and all(reply.get(k) == expected.get(k) for k in IDENTITY_FIELDS)
        stage = self.contract["stages"].get(expected.get("stage"), {})
        valid = valid and reply.get("valid") is True and reply.get("decision") in LABELS
        valid = valid and reply.get("decision_policy") == self.contract["decision_policy"]
        valid = valid and reply.get("message_layout") == MESSAGE_LAYOUT
        valid = valid and reply.get("prompt_sha256") == stage.get("prompt_sha256")
        valid = valid and reply.get("camera_ids") == stage.get("camera_ids") and reply.get("modality") == stage.get("modality")
        captured = expected.get("captured_at_ms", -1)
        valid = valid and isinstance(captured, (int, float)) and math.isfinite(captured)
        valid = valid and 0 <= now_ms - captured <= self.contract["decision_policy"]["max_reply_age_seconds"] * 1000
        valid = valid and expected.get("request_id") != self.last_request and captured > self.last_capture
        if not valid:
            self.green_count = 0
            return "A"
        self.last_request, self.last_capture = expected["request_id"], captured
        if reply["decision"] != "G":
            self.green_count = 0
            return reply["decision"]
        self.green_count += 1
        return "G" if self.green_count >= self.contract["decision_policy"]["consecutive_green"] else "A"
