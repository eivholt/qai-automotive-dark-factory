"""Prepare private, traceable motor excerpts from a user-supplied recording.

Input is a local audio file plus its source.json provenance. Requires numpy,
scipy and imageio-ffmpeg. --dependencies may point to a private pip --target
directory. This utility makes no generation API calls and changes no UE assets.
"""
from __future__ import annotations

import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import wave

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def decode(path, rate):
    import imageio_ffmpeg
    import numpy as np
    result = subprocess.run([imageio_ffmpeg.get_ffmpeg_exe(), "-nostdin", "-v", "error",
                             "-i", str(path), "-ac", "1", "-ar", str(rate),
                             "-f", "f32le", "-c:a", "pcm_f32le", "pipe:1"],
                            check=True, capture_output=True)
    samples = np.frombuffer(result.stdout, dtype="<f4").astype(np.float64)
    if not len(samples) or not np.isfinite(samples).all():
        raise RuntimeError("Invalid decoded recording")
    return samples


def level_and_write(path, samples, rate, target):
    import numpy as np
    samples = samples.copy()
    samples -= np.mean(samples)
    peak = float(np.abs(samples).max())
    if peak < 1e-6:
        raise RuntimeError("Silent extracted segment")
    gain = 10 ** (target / 20) / peak
    samples *= gain
    fade = min(round(rate * .01), len(samples) // 2)
    samples[:fade] *= np.linspace(0, 1, fade)
    samples[-fade:] *= np.linspace(1, 0, fade)
    if not np.isfinite(samples).all() or np.max(np.abs(samples)) >= 1:
        raise RuntimeError("Invalid extracted preview level")
    pcm = np.round(samples * 32767).astype("<i2")
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(1)
        stream.setsampwidth(2)
        stream.setframerate(rate)
        stream.writeframes(pcm.tobytes())
    with wave.open(str(path), "rb") as stream:
        restored = np.frombuffer(stream.readframes(stream.getnframes()), dtype="<i2").astype(float) / 32768
        if len(restored) != len(samples) or stream.getframerate() != rate:
            raise RuntimeError("Extracted WAV readback mismatch")
    return {"duration_seconds": len(samples) / rate, "sample_rate": rate, "channels": 1,
            "source_gain_db": float(20 * np.log10(gain)), "edge_fade_ms": 10,
            "peak_dbfs": float(20 * np.log10(max(1e-12, np.abs(restored).max()))),
            "rms_dbfs": float(20 * np.log10(max(1e-12, np.sqrt(np.mean(restored ** 2))))),
            "sha256": sha(path)}


def cleanup(samples, rate, recipe):
    import numpy as np
    from scipy.ndimage import gaussian_filter
    from scipy.signal import butter, sosfiltfilt, stft, istft
    highpass = butter(recipe["highpass_order"], recipe["highpass_hz"],
                      btype="highpass", fs=rate, output="sos")
    filtered = sosfiltfilt(highpass, samples)
    start = round(recipe["noise_reference_start_seconds"] * rate)
    end = start + round(recipe["noise_reference_duration_seconds"] * rate)
    if not 0 <= start < end <= len(samples):
        raise RuntimeError("Invalid background reference interval")
    kwargs = {"fs": rate, "nperseg": 2048, "noverlap": 1536}
    _, _, spectrum = stft(filtered, **kwargs)
    _, _, reference = stft(filtered[start:end], **kwargs)
    noise = np.median(np.abs(reference) ** 2, axis=1)[:, None]
    floor = 10 ** (-recipe["maximum_spectral_attenuation_db"] / 20)
    power = np.abs(spectrum) ** 2
    gain = np.sqrt(np.clip(1 - recipe["noise_subtraction_strength"] * noise /
                           np.maximum(power, 1e-18), floor ** 2, 1))
    gain = gaussian_filter(gain, sigma=(1.5, 1))
    _, restored = istft(spectrum * gain, **kwargs)
    return restored[:len(samples)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--metadata", type=Path, required=True)
    parser.add_argument("--plan", type=Path, default=ROOT / "configs/recorded_robot_review.json")
    parser.add_argument("--previous", type=Path, default=ROOT / "assets/audio/review-20261022/revision-3")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/audio/review-20261022/revision-4")
    parser.add_argument("--dependencies", type=Path)
    args = parser.parse_args()
    if args.dependencies:
        sys.path.insert(0, str(args.dependencies.resolve()))
    import numpy as np
    from generate_sound_review import edit_charge
    output, source, previous = args.output.resolve(), args.input.resolve(), args.previous.resolve()
    if not all(p.is_relative_to(ROOT / "assets") for p in (output, source, previous)):
        raise RuntimeError("Recording, prior batch and output must remain in private assets")
    plan = json.loads(args.plan.read_text(encoding="utf-8"))
    metadata = json.loads(args.metadata.read_text(encoding="utf-8"))
    if metadata["source_sha256"] != sha(source) or metadata["id"] != plan["reference_id"]:
        raise RuntimeError("Recording provenance mismatch")
    prior = json.loads((previous / "manifest.json").read_text(encoding="utf-8"))
    prior_clips = {c["id"]: c for c in prior["clips"]}
    output.mkdir(parents=True, exist_ok=True)
    previews = output / "previews"
    previews.mkdir(exist_ok=True)
    rate = plan["sample_rate"]
    samples = decode(source, rate)
    clean = cleanup(samples, rate, plan["cleanup"])
    manifest = {"title": plan["title"], "source": metadata, "plan_sha256": sha(args.plan),
                "purpose": "Private review only; no Unreal integration", "clips": [],
                "cleanup": plan["cleanup"], "generation_api_calls": 0}
    for excerpt in plan["excerpts"]:
        start = round(excerpt["start_seconds"] * rate)
        end = start + round(excerpt["duration_seconds"] * rate)
        if not 0 <= start < end <= len(samples):
            raise RuntimeError("Excerpt outside source recording")
        path = previews / (excerpt["id"] + ".wav")
        measurements = level_and_write(path, (clean if excerpt["cleanup"] else samples)[start:end],
                                       rate, plan["motor_peak_dbfs"])
        manifest["clips"].append({**excerpt, "preview": path.relative_to(output).as_posix(),
                                  "source_end_seconds": end / rate, "measurements": measurements,
                                  "pitch_or_speed_changes": False})
    # Preserve the approved NIR WAV byte for byte. Thermal uses its original
    # charging source with a clearly different octave and the same short length.
    nir = prior_clips["nir_enable"]
    nir_preview = previous / nir["preview"]
    nir_source = previous / nir["source"]
    if sha(nir_preview) != nir["measurements"]["sha256"] or sha(nir_source) != nir["source_sha256"]:
        raise RuntimeError("Approved NIR source or preview changed")
    shutil.copyfile(nir_preview, previews / "nir_enable.wav")
    manifest["clips"].append({"id": "nir_enable", "label": "NIR — approved short charge",
        "preview": "previews/nir_enable.wav", "measurements": nir["measurements"], "unchanged": True})
    thermal = edit_charge(decode(nir_source, rate)[:, None], rate, plan["thermal"])[:, 0]
    measurements = level_and_write(previews / "thermal_enable.wav", thermal, rate, -3)
    manifest["clips"].append({"id": "thermal_enable", "label": "Thermal — one octave lower",
        "preview": "previews/thermal_enable.wav", "measurements": measurements,
        "source_sha256": nir["source_sha256"], "offline_edit": plan["thermal"]})
    cards = []
    for clip in manifest["clips"]:
        note = ""
        comparison = ""
        if clip["id"].startswith("recorded_"):
            note = (f'Source {clip["start_seconds"]:.1f}–{clip["source_end_seconds"]:.1f} s · '
                    + ("70 Hz high-pass and gentle background reduction" if clip["cleanup"] else "No noise filtering")
                    + ". Original pitch and speed; level adjustment and short edge fades.")
            if clip["id"] == "recorded_travel":
                old = Path(os.path.relpath(previous / prior_clips["robot_travel"]["preview"], output)).as_posix()
                comparison = f'<details><summary>Compare rejected generated take</summary><audio controls preload="none" src="{old}"></audio></details>'
        elif clip["id"] == "thermal_enable":
            note = "Same charging source as NIR; pitch halved (−12 semitones). Both cues last 1.5 seconds."
            old = Path(os.path.relpath(previous / prior_clips["thermal_enable"]["preview"], output)).as_posix()
            comparison = f'<details><summary>Compare previous thermal pitch</summary><audio controls preload="none" src="{old}"></audio></details>'
        else:
            note = "Approved NIR preview preserved unchanged, for comparison with thermal."
        cards.append(f'<article><h2>{html.escape(clip["label"])}</h2><p>{html.escape(note)}</p>'
                     f'<audio controls preload="metadata" src="{clip["preview"]}"></audio>'
                     + comparison + f'<p><a href="{clip["preview"]}" download>Download preview WAV</a></p></article>')
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Recorded robot sound review</title><style>
:root{color-scheme:dark;font:16px system-ui;background:#111723;color:#e9eef8}body{max-width:1140px;margin:32px auto;padding:0 22px}h1{font-size:30px}h2{font-size:20px}p{color:#b9c7da;line-height:1.6}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(310px,1fr));gap:16px}article{background:#1c2635;border:1px solid #3d4c61;border-radius:12px;padding:20px}audio{width:100%}a{color:#a7c4ff}summary{cursor:pointer;color:#bccff1}</style>'''
    page += '<h1>' + html.escape(plan["title"]) + '</h1>'
    page += ('<p>Motor excerpts from the user-supplied <a href="' + html.escape(plan["reference_url"], quote=True)
             + '">' + html.escape(metadata["title"]) + '</a> by ' + html.escape(metadata["uploader"])
             + '. The first two players compare the same interval before and after gentle cleanup. '
             'Room sound remains; these are not isolated studio stems. All extracts retain original pitch and speed. '
             'This is a private review, separate from Unreal.</p><div class="grid">')
    page += "".join(cards)
    page += ('</div><p><a href="../revision-3/">Other existing effects</a> · <a href="manifest.json">Source and processing details</a></p>'
             '<script>document.querySelectorAll("audio").forEach(a=>a.addEventListener("play",()=>document.querySelectorAll("audio").forEach(b=>{if(a!==b)b.pause()})));</script></html>')
    (output / "index.html").write_text(page, encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"clips": len(manifest["clips"]), "source_seconds": len(samples) / rate,
                      "review": str(output / "index.html"), "generation_api_calls": 0}))


if __name__ == "__main__":
    main()
