"""Bounded preparation of actual images; the final gate contract stays unchanged."""
from collections import OrderedDict
from dataclasses import dataclass, field
import hashlib
import io
from pathlib import Path
import tempfile
import time

from .gates import IDENTITY_FIELDS, decode_gate_image, validate_gate_identity
from .protocol import ProtocolError, valid_identifier

STREAM_PROTOCOL = "ordered-three-view-prefill-v1"
STREAM_FIELDS = {*IDENTITY_FIELDS, "session_id", "epoch", "prompt_sha256"}


def parse_stream(payload, contract, image=False):
    expected = STREAM_FIELDS | ({"image"} if image else set())
    if not isinstance(payload, dict) or set(payload) != expected:
        raise ProtocolError("Unexpected stream envelope fields")
    validate_gate_identity(payload, contract)
    if not valid_identifier(payload["session_id"]) or type(payload["epoch"]) is not int or not 1 <= payload["epoch"] < 2**53:
        raise ProtocolError("Invalid stream session/epoch")
    stage = contract["stages"][payload["stage"]]
    if payload["prompt_sha256"] != stage["prompt_sha256"]:
        raise ProtocolError("Stream prompt mismatch")
    envelope = {key: payload[key] for key in STREAM_FIELDS}
    if not image:
        return envelope
    item = payload["image"]
    if not isinstance(item, dict) or item.get("camera_id") not in stage["camera_ids"]:
        raise ProtocolError("Unknown stream camera")
    index = stage["camera_ids"].index(item["camera_id"])
    return envelope, index, decode_gate_image(item, stage["camera_ids"][index])


def grayscale(png):
    from PIL import Image, ImageChops
    with Image.open(io.BytesIO(png)) as image:
        if image.size != (448, 256) or image.format != "PNG":
            raise ProtocolError("Expected sensor PNG")
        if "A" in image.getbands() and image.getchannel("A").getextrema() != (255, 255):
            raise ProtocolError("Sensor alpha must be opaque")
        red, green, blue = image.convert("RGB").split()
        if ImageChops.difference(red, green).getbbox() or ImageChops.difference(red, blue).getbbox():
            raise ProtocolError("Observer palettes are not model input")


@dataclass
class PreparedBatch:
    envelope: dict
    native_id: str
    role: str
    started: float
    hashes: list = field(default_factory=list)
    preparations: list = field(default_factory=list)


class StreamCoordinator:
    """Caller holds one service lock across every operation, including native IO."""
    def __init__(self, contract, native, clock=time.monotonic):
        self.contract, self.native, self.clock = contract, native, clock
        self.active = None
        self.retired, self.sessions = OrderedDict(), OrderedDict()
        self.serial = 0

    @staticmethod
    def key(envelope):
        return envelope["session_id"], envelope["epoch"]

    def prune(self):
        now = self.clock()
        for cache in (self.retired, self.sessions):
            for key in list(cache):
                if cache[key][-1] <= now:
                    del cache[key]

    def remember(self, envelope):
        self.prune()
        key = self.key(envelope)
        if key not in self.retired and len(self.retired) >= 64:
            raise ProtocolError("Stream retirement capacity reached")
        self.retired[key] = (self.clock() + 32,)

    def advance_epoch(self, envelope):
        self.prune()
        session, epoch = self.key(envelope)
        prior = self.sessions.get(session)
        if prior and epoch <= prior[0]:
            raise ProtocolError("Replayed stream epoch")
        if not prior and len(self.sessions) >= 16:
            raise ProtocolError("Too many stream sessions")
        self.sessions[session] = (epoch, self.clock() + 60)

    def discard(self, cancel=True):
        batch, self.active = self.active, None
        if batch:
            try:
                self.remember(batch.envelope)
            finally:
                if cancel:
                    self.native(batch.role, "cancel", batch_id=batch.native_id)

    def expire(self):
        self.prune()
        if self.active and self.clock() - self.active.started >= self.contract["decision_policy"]["max_reply_age_seconds"]:
            self.discard()

    def prepare(self, payload):
        envelope, index, png = parse_stream(payload, self.contract, image=True)
        grayscale(png)
        self.expire()
        key = self.key(envelope)
        if key in self.retired:
            raise ProtocolError("Retired stream epoch")
        if self.active and self.key(self.active.envelope) != key:
            old = self.active.envelope
            if index != 0 or old["session_id"] != envelope["session_id"] or old["epoch"] >= envelope["epoch"]:
                raise ProtocolError("Another stream batch is active")
            self.discard()
        if not self.active:
            if index != 0:
                raise ProtocolError("Stream must begin with spot1")
            self.advance_epoch(envelope)
            self.serial += 1
            role = "weld" if envelope["stage"] == "weld" else "primary"
            self.active = PreparedBatch(envelope, f"stream-{self.serial}", role, self.clock())
            try:
                self.native(role, "begin", batch_id=self.active.native_id, image_count=3,
                            prompt=self.contract["stages"][envelope["stage"]]["prompt"])
            except Exception:
                self.discard(cancel=False)
                raise
        batch = self.active
        if batch.envelope != envelope or index != len(batch.hashes):
            raise ProtocolError("Stream identity/order mismatch")
        started = self.clock()
        try:
            with tempfile.TemporaryDirectory(prefix="automotive-stream-") as temporary:
                path = Path(temporary) / "sensor.png"
                path.write_bytes(png)
                result = self.native(batch.role, "append", batch_id=batch.native_id, index=index, image=str(path))
            if result.get("prepared") is not True or result.get("received") != index + 1:
                raise RuntimeError("Invalid native preparation acknowledgment")
            batch.hashes.append(hashlib.sha256(png).hexdigest())
            batch.preparations.append({"index": index, "elapsed_ms": (self.clock() - started) * 1000,
                                       "early_prefill_tokens": result.get("early_prefill_tokens", 0)})
            if self.clock() - batch.started >= 8:
                self.discard()
                raise ProtocolError("Preparation expired")
        except Exception:
            if self.active is batch:
                self.discard(cancel=False)
            raise
        # Acknowledgment is explicitly not a gate result.
        return {**envelope, "prepared": True, "protocol": STREAM_PROTOCOL,
                "index": index, "camera_id": payload["image"]["camera_id"], "png_sha256": batch.hashes[-1]}

    def cancel(self, payload):
        envelope = parse_stream(payload, self.contract)
        key = self.key(envelope)
        if self.active and self.key(self.active.envelope) == key:
            if self.active.envelope != envelope:
                raise ProtocolError("Cancellation identity mismatch")
            self.discard()
        else:
            self.remember(envelope)  # Also catches cancel-before-prepare network reordering.
        return {**envelope, "cancelled": True, "protocol": STREAM_PROTOCOL}

    def finish(self, request, session, epoch):
        self.expire()
        batch = self.active
        if not batch or self.key(batch.envelope) != (session, epoch):
            raise ProtocolError("No matching prepared batch")
        if any(batch.envelope[k] != request.identity[k] for k in IDENTITY_FIELDS):
            raise ProtocolError("Final stream identity mismatch")
        hashes = [hashlib.sha256(png).hexdigest() for png in request.pngs]
        if len(batch.hashes) != 3 or hashes != batch.hashes:
            self.discard()
            raise ProtocolError("Final PNGs differ from prepared images")
        try:
            result = self.native(batch.role, "score", batch_id=batch.native_id)
            if self.clock() - batch.started >= 8:
                raise ProtocolError("Stream result expired")
            if result.get("early_prefill_tokens") != 128 or result.get("visual_tokens") != 336:
                raise RuntimeError("Incomplete joint streaming inference")
            metadata = {"protocol": STREAM_PROTOCOL, "session_id": session, "epoch": epoch,
                "prepared_images": 3, "image_sha256": hashes, "early_prefill_tokens": 128,
                "preparations": batch.preparations, "server_batch_ms": (self.clock() - batch.started) * 1000}
            return result, metadata
        finally:
            self.discard(cancel=False)  # Native score consumes the batch, including on error.
