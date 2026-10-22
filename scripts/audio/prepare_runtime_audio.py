"""Pack approved private WAVs for the six-source Unreal audio mixer.

Requires local imageio-ffmpeg/numpy (e.g. assets/audio/.tools). Generated bank,
source provenance and audition files remain private. No network/API requests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
# Order is the C++ EFactoryClip contract. Short tool events select one energetic
# contact/pulse from the approved take, rather than playing a three-weld montage.
CLIPS = [
    ("motor", "revision-4", "recorded_travel_clean", 0, True),
    ("arrival", "revision-3", "sled_arrival", 0, False),
    ("departure", "revision-3", "sled_departure", 0, False),
    ("clamp", "revision-3", "pinch_clamp", .18, False),
    ("weld", "revision-3", "spot_welding", .50, False),
    ("shutter", "revision-3", "inspection_shutter", 0, False),
    ("pickup", "revision-3", "panel_pickup", 1., False),
    ("placement", "revision-3", "panel_placement", .7, False),
    ("sealant", "revision-3", "sealant_dispense", 0, False),
    ("cooling", "revision-3", "weld_cooling", 0, False),
    ("roof", "revision-3", "roof_lights", 0, False),
    ("nir", "revision-4", "nir_enable", 0, False),
    ("thermal", "revision-4", "thermal_enable", 0, False),
    ("room", "revision-3", "factory_ambience", 0, False),
    ("fan", "revision-3", "extraction_fan", 0, False),
    ("cabinet", "revision-3", "cabinet_hum", 0, False),
    ("release", "revision-3", "gripper_release", .5, False),
]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--dependencies", type=Path, default=ROOT / "assets/audio/.tools")
    p.add_argument("--reviews", type=Path, default=ROOT / "assets/audio/review-20261022")
    p.add_argument("--output", type=Path, default=ROOT / "assets/audio/runtime")
    a = p.parse_args()
    sys.path.insert(0, str(a.dependencies.resolve()))
    import numpy as np
    import imageio_ffmpeg
    if not a.output.resolve().is_relative_to(ROOT / "assets"):
        raise ValueError("Output must remain private")
    rate = 48000
    bank = bytearray(b"DFAU0001" + struct.pack("<II", rate, len(CLIPS)))
    rows = []
    for index, (name, revision, clip, trim, loop) in enumerate(CLIPS):
        source = a.reviews / revision / "previews" / (clip + ".wav")
        decoded = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-nostdin", "-v", "error",
            "-i", str(source), "-ac", "1", "-ar", str(rate), "-f", "f32le", "pipe:1"],
            check=True, capture_output=True).stdout
        samples = np.frombuffer(decoded, dtype="<f4").astype(float)
        if not np.isfinite(samples).all() or not .1 <= len(samples) / rate <= 30:
            raise ValueError("Invalid approved sound " + name)
        start = 0
        if trim:
            count = round(trim * rate)
            energy = np.concatenate(([0.], np.cumsum(samples * samples)))
            scores = energy[count:] - energy[:-count]
            start = int(np.argmax(scores))
            samples = samples[start:start+count].copy()
        if loop:
            count = round(.1 * rate)
            theta = np.linspace(0, np.pi / 2, count)
            join = samples[-count:] * np.cos(theta) + samples[:count] * np.sin(theta)
            samples = np.concatenate((samples[count:-count], join))
        else:
            count = min(round(.006 * rate), len(samples) // 2)
            samples[:count] *= np.linspace(0, 1, count)
            samples[-count:] *= np.linspace(1, 0, count)
        # Resampling can expose intersample peaks. Retain levels unless headroom
        # is actually needed; record the protective constant gain.
        peak = float(np.max(np.abs(samples)))
        headroom_gain = min(1., .95 / max(peak, 1e-12))
        samples *= headroom_gain
        pcm = np.round(samples * 32767).astype("<i2")
        bank.extend(struct.pack("<II", index, len(pcm)))
        bank.extend(pcm.tobytes())
        rows.append({"id": index, "name": name, "source": str(source),
                     "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                     "source_start_seconds": start / rate, "duration_seconds": len(samples) / rate,
                     "loop_overlap_seconds": .1 if loop else 0,
                     "resample_headroom_gain": headroom_gain,
                     "peak": float(np.max(np.abs(samples)))})
    a.output.mkdir(parents=True, exist_ok=True)
    pending = a.output / "bank.dfau.tmp"
    pending.write_bytes(bank)
    pending.replace(a.output / "bank.dfau")
    metadata = {"schema": "darkfactory-private-audio-v1", "sample_rate": rate,
                "bank_sha256": hashlib.sha256(bank).hexdigest(), "clips": rows}
    (a.output / "manifest.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"clips": len(rows), "pcm_bytes": len(bank), "output": str(a.output)}))


if __name__ == "__main__":
    main()
