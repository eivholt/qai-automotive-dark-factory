"""Pack approved private WAVs for the six-source Unreal audio mixer.

Requires local imageio-ffmpeg/numpy (e.g. assets/audio/.tools). Generated bank,
source provenance and audition files remain private. No network/API requests.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]
# Order is the C++ EFactoryClip contract. Short tool events select one energetic
# contact/pulse from the approved take, rather than playing a three-weld montage.
CLIPS = [
    ("motor", "revision-4", "recorded_travel_clean", 0, True),
    ("arrival", "revision-3", "sled_arrival", 0, False),
    ("departure", "revision-3", "sled_departure", 0, False),
    ("clamp", "revision-3", "pinch_clamp", 0, False),
    ("weld", "revision-3", "spot_welding", 0, False),
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
    ("pinch_release", "revision-3", "pinch_clamp", 0, False),
    ("weld_alt_1", "revision-3", "spot_welding", 0, False),
    ("weld_alt_2", "revision-3", "spot_welding", 0, False),
]


def fit_duration(samples, count, rate, ffmpeg, np):
    """Pitch-preserving offline compression; runtime never stretches a sound."""
    tempo = len(samples) / count
    stages = []
    while tempo > 2:
        stages.append("atempo=2")
        tempo /= 2
    stages.append(f"atempo={tempo:.12g}")
    result = subprocess.run([ffmpeg, "-nostdin", "-v", "error", "-f", "f32le",
        "-ar", str(rate), "-ac", "1", "-i", "pipe:0", "-af", ",".join(stages),
        "-f", "f32le", "pipe:1"], input=samples.astype("<f4").tobytes(),
        check=True, capture_output=True).stdout
    fitted = np.frombuffer(result, dtype="<f4").astype(float)
    return np.pad(fitted[:count], (0, max(0, count-len(fitted))))


def phase_edit(name, samples, rate, ffmpeg, np):
    """Shape the approved foley around the actual authored mechanism phases."""
    if name == "roof":
        header = (ROOT / "unreal/Source/DarkFactory/DarkFactoryFluorescentTiming.h").read_text()
        seconds = float(re.search(r"DurationSeconds\s*=\s*([\d.]+)f", header)[1])
        samples = fit_duration(samples, round(seconds*rate), rate, ffmpeg, np)
        fade = round(.08*rate)
        samples[-fade:] *= np.linspace(1, 0, fade)
        return samples, {"edit": "pitch-preserving fluorescent startup fit",
                         "duration_source": "DarkFactoryFluorescentTiming.h",
                         "runtime_envelope": "shared six-bank strike curve"}
    if name in ("clamp", "pinch_release"):
        start, length, seconds = (0, .60, .15) if name == "clamp" else (.40, .24, .12)
        samples = samples[round(start*rate):round((start+length)*rate)].copy()
        samples = fit_duration(samples, round(seconds*rate), rate, ffmpeg, np)
        if name == "pinch_release":
            samples *= .65
        return samples, {"edit": "fit jaw actuator/contact to actual squeeze/release",
                         "source_start_seconds": start, "source_length_seconds": length}
    if name.startswith("weld"):
        variant = {"weld": 0, "weld_alt_1": 1, "weld_alt_2": 2}[name]
        start = (0., .95, 2.20)[variant]
        samples = samples[round(start*rate):round((start+.5)*rate)].copy()
        t = np.arange(len(samples))/rate
        # Illustrative low transformer body, not measured drive acoustics. Keep
        # recorded crackle, with an explicit current onset/decay instead of a
        # loudness-maximizing middle cut. No continuous arc between welds.
        body = .08*(np.sin(2*np.pi*100*t)+.28*np.sin(2*np.pi*300*t))
        body *= 1+.10*np.sin(2*np.pi*23*t+variant)
        attack = np.minimum(t/.004, 1)
        release = np.minimum((.5-t)/.014, 1)
        envelope = np.maximum(0, attack*release)
        samples = (samples*1.30+body)*envelope
        return samples, {"edit": "recorded crackle plus shaped transformer body",
                         "source_start_seconds": start, "current_seconds": .5,
                         "variant": variant, "illustrative_body_hz": [100, 300]}
    return samples, {}


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
    prepared = {}
    for index, (name, revision, clip, trim, loop) in enumerate(CLIPS):
        source = a.reviews / revision / "previews" / (clip + ".wav")
        decoded = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-nostdin", "-v", "error",
            "-i", str(source), "-ac", "1", "-ar", str(rate), "-f", "f32le", "pipe:1"],
            check=True, capture_output=True).stdout
        samples = np.frombuffer(decoded, dtype="<f4").astype(float)
        if not np.isfinite(samples).all() or not .1 <= len(samples) / rate <= 30:
            raise ValueError("Invalid approved sound " + name)
        samples, edit = phase_edit(name, samples, rate, imageio_ffmpeg.get_ffmpeg_exe(), np)
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
        prepared[name] = samples.copy()
        bank.extend(struct.pack("<II", index, len(pcm)))
        bank.extend(pcm.tobytes())
        rows.append({"id": index, "name": name, "source": str(source),
                     "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                     "source_start_seconds": edit.get("source_start_seconds", start / rate),
                     "duration_seconds": len(samples) / rate, "phase_edit": edit,
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
    # Private dry audition of the six-spot rhythm. Live playback additionally
    # applies spatial attenuation, the source limiter and the +10 dB mix gain.
    sequence = np.zeros(round(8.0*rate))
    for spot in range(6):
        contact = .15+spot*1.35
        for name, at, gain in [("clamp", contact-.15, .12),
            (("weld", "weld_alt_1", "weld_alt_2")[spot%3], contact, .16),
            ("pinch_release", contact+.58, .10)]:
            offset = round(at*rate)
            sequence[offset:offset+len(prepared[name])] += prepared[name]*gain
    def wav(name, data):
        with wave.open(str(a.output / name), "wb") as w:
            w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
            w.writeframes(np.round(data*32767).astype("<i2").tobytes())
    wav("weld_sequence_preview.wav", sequence*min(3.16227766, .9/max(abs(sequence))))
    wav("roof_startup_preview.wav", prepared["roof"])
    print(json.dumps({"clips": len(rows), "pcm_bytes": len(bank), "output": str(a.output)}))


if __name__ == "__main__":
    main()
