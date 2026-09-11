"""One visual-only inspection contract shared by inference and dataset tooling."""

from __future__ import annotations

import base64
import binascii
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import sys
from typing import Any
import zlib

DECISIONS = frozenset({"G", "A", "R"})
MAX_PNG_BYTES = 1024 * 1024
MAX_REQUEST_BYTES = 1536 * 1024
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.:-]{0,127}\Z")
PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


class ProtocolError(ValueError):
    """Malformed or incompatible input; the caller must hold the simulation."""


def load_contract(path: str | Path | None = None) -> dict[str, Any]:
    if path is None:
        candidates = [
            Path(__file__).resolve().parents[2] / "configs" / "inspection_contract.json",
            Path(sys.prefix) / "share" / "qai-automotive-dark-factory" / "inspection_contract.json",
        ]
        path = next((candidate for candidate in candidates if candidate.is_file()), candidates[0])
    contract = json.loads(Path(path).read_text(encoding="utf-8"))
    if not isinstance(contract, dict) or not isinstance(contract.get("prompt"), str):
        raise ProtocolError("Contract must contain a fixed text prompt")
    if not contract["prompt"] or not isinstance(contract.get("version"), str):
        raise ProtocolError("Contract prompt and version are required")
    if contract.get("prompt_sha256") != prompt_sha256(contract):
        raise ProtocolError("Contract prompt_sha256 does not match its prompt")
    if set(contract.get("labels", {})) != DECISIONS:
        raise ProtocolError("Contract labels must be exactly G, A, R")
    image = contract.get("image", {})
    if (image.get("width"), image.get("height"), image.get("bit_depth"), image.get("format")) != (448, 256, 8, "PNG"):
        raise ProtocolError("This bridge requires 448x256 8-bit PNG inputs")
    expected_generation = {"max_completion_tokens": 1, "temperature": 0, "enable_think": False, "top_k": 1, "seed": 42, "stream": False}
    if contract.get("generation") != expected_generation:
        raise ProtocolError("Contract generation must use deterministic one-letter inference")
    return contract


def prompt_sha256(contract: dict[str, Any]) -> str:
    return hashlib.sha256(contract["prompt"].encode("utf-8")).hexdigest()


def valid_identifier(value: Any) -> bool:
    return isinstance(value, str) and IDENTIFIER.fullmatch(value) is not None


def strict_decision(content: Any) -> str:
    # Whitespace is a transport/tokenization detail; prose, JSON, lower case,
    # thinking tags, and multiple letters are never guessed into a decision.
    if not isinstance(content, str) or content.strip() not in DECISIONS:
        raise ProtocolError("Model did not return exactly one of G, A, R")
    return content.strip()


def validate_png(data: bytes, *, width: int = 448, height: int = 256) -> None:
    """Validate PNG framing, CRCs, dimensions, and bounded pixel decompression.

    Accept non-interlaced 8-bit gray/RGB/gray-alpha/RGBA. PNG is always lossless;
    indexed, interlaced and animated inputs are outside this small camera contract.
    Pixel values are not changed and contain no simulator state or overlays.
    """
    if not 45 <= len(data) <= MAX_PNG_BYTES or not data.startswith(PNG_SIGNATURE):
        raise ProtocolError("Image must be a PNG no larger than 1 MiB")
    offset, color_type, seen_ihdr, seen_iend = 8, None, False, False
    compressed = bytearray()
    idat_finished = False
    while offset < len(data):
        if offset + 12 > len(data):
            raise ProtocolError("Truncated PNG chunk")
        size = struct.unpack_from(">I", data, offset)[0]
        kind = data[offset + 4:offset + 8]
        end = offset + 12 + size
        if end > len(data):
            raise ProtocolError("Truncated PNG chunk")
        payload = data[offset + 8:offset + 8 + size]
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        if zlib.crc32(kind + payload) & 0xFFFFFFFF != crc:
            raise ProtocolError("PNG checksum mismatch")
        if not seen_ihdr and kind != b"IHDR":
            raise ProtocolError("PNG must start with IHDR")
        if kind == b"IHDR":
            if seen_ihdr or size != 13:
                raise ProtocolError("Invalid PNG header")
            w, h, depth, color_type, compression, filtering, interlace = struct.unpack(">IIBBBBB", payload)
            if (w, h) != (width, height):
                raise ProtocolError(f"Image must be exactly {width}x{height}")
            if depth != 8 or color_type not in (0, 2, 4, 6) or (compression, filtering, interlace) != (0, 0, 0):
                raise ProtocolError("PNG must use non-interlaced 8-bit gray or RGB pixels")
            seen_ihdr = True
        elif kind == b"IDAT":
            if idat_finished:
                raise ProtocolError("PNG IDAT chunks must be contiguous")
            compressed.extend(payload)
        elif kind == b"IEND":
            if size or not compressed or end != len(data):
                raise ProtocolError("Invalid PNG end")
            seen_iend = True
        elif kind == b"acTL":
            raise ProtocolError("Animated PNG inputs are not supported")
        elif kind not in (b"PLTE",) and not kind[0] & 0x20:
            raise ProtocolError("Unsupported critical PNG chunk")
        if compressed and kind != b"IDAT":
            idat_finished = True
        offset = end
    if not seen_ihdr or not seen_iend or color_type is None:
        raise ProtocolError("Incomplete PNG")
    channels = {0: 1, 2: 3, 4: 2, 6: 4}[color_type]
    stride = width * channels + 1
    expected = height * stride
    try:
        inflater = zlib.decompressobj()
        pixels = inflater.decompress(compressed, expected + 1)
    except zlib.error as exc:
        raise ProtocolError("PNG pixels are corrupt") from exc
    if len(pixels) != expected or not inflater.eof or inflater.unused_data or inflater.unconsumed_tail:
        raise ProtocolError("PNG pixel data has invalid size or framing")
    if any(pixels[row * stride] > 4 for row in range(height)):
        raise ProtocolError("Invalid PNG row filter")


@dataclass(frozen=True)
class InspectionRequest:
    request_id: str
    episode_id: str
    image_base64: str


def parse_inspection(payload: Any) -> InspectionRequest:
    if not isinstance(payload, dict) or set(payload) != {"request_id", "episode_id", "image_base64"}:
        raise ProtocolError("Expected only request_id, episode_id, image_base64")
    for field in ("request_id", "episode_id"):
        if not valid_identifier(payload[field]):
            raise ProtocolError(f"{field} must be 1-128 ASCII letters, digits, dot, colon, underscore or hyphen")
    encoded = payload["image_base64"]
    if not isinstance(encoded, str) or len(encoded) > ((MAX_PNG_BYTES + 2) // 3) * 4:
        raise ProtocolError("image_base64 must contain a small PNG as bare base64")
    try:
        data = base64.b64decode(encoded, validate=True)
    except (binascii.Error, ValueError) as exc:
        raise ProtocolError("image_base64 is not valid bare base64") from exc
    validate_png(data)
    return InspectionRequest(payload["request_id"], payload["episode_id"], encoded)
