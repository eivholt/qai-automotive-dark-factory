"""Generate private ElevenLabs auditions; never changes the Unreal soundscape.

Use --generate explicitly for paid requests. An unfinished request is never
automatically repeated. Credentials are read locally and never saved in output.
Requires numpy and soundfile for the offline preview pass.
"""
from __future__ import annotations

import argparse
from datetime import datetime, timezone
from decimal import Decimal
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import sys
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
BASE = "https://api.elevenlabs.io"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(output, manifest):
    pending = output / "manifest.json.tmp"
    pending.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    pending.replace(output / "manifest.json")


def read_key(path):
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^\s*(?:export\s+)?ELEVENLABS_API_KEY\s*=\s*(.*?)\s*$", line)
        if match:
            value = match[1]
            if value[:1] in ("'", '"') and value[-1:] == value[:1]:
                value = value[1:-1]
            else:
                value = re.split(r"\s+#", value, maxsplit=1)[0].strip()
            if value and not any(c.isspace() for c in value):
                return value
    raise RuntimeError("Missing or invalid local ElevenLabs key; value withheld")


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def api(key, path, payload=None):
    body = None if payload is None else json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(BASE + path, data=body, headers={
        "xi-api-key": key, "Content-Type": "application/json",
        "User-Agent": "DarkFactory-Offline-Audition/1.0",
    })
    try:
        with urllib.request.build_opener(NoRedirect).open(request, timeout=150) as response:
            return {k.lower(): v for k, v in response.headers.items()}, response.read()
    except urllib.error.HTTPError as error:
        raise RuntimeError(f"ElevenLabs HTTP {error.code}; no automatic retry") from None
    except (urllib.error.URLError, TimeoutError):
        raise RuntimeError("API connection failed; preserve manifest before any retry") from None


def subscription(key):
    _, body = api(key, "/v1/user/subscription")
    data = json.loads(body)
    return {k: data.get(k) for k in ("tier", "status", "character_count", "character_limit")}


def write_audio(path, samples, rate):
    import numpy as np
    import soundfile as sf
    if not np.isfinite(samples).all() or np.max(np.abs(samples)) >= 1:
        raise RuntimeError("Invalid preview samples")
    sf.write(path, samples, rate, subtype="PCM_24")
    restored, actual_rate = sf.read(path, always_2d=True)
    if restored.shape != samples.shape or actual_rate != rate:
        raise RuntimeError("Audio readback format mismatch")
    return {"duration_seconds": len(samples) / rate, "sample_rate": rate,
            "channels": samples.shape[1], "peak_dbfs": float(20 * np.log10(max(1e-12, np.abs(restored).max()))),
            "rms_dbfs": float(20 * np.log10(max(1e-12, np.sqrt(np.mean(restored ** 2))))),
            "sha256": digest(path)}


def edit_charge(samples, rate, edit):
    """Keep the charge attack/release, removing the long middle sustain.

    A downward varispeed shift supplies a related thermal variant. This
    is an authored interface cue, not simulated capacitor electronics.
    """
    import numpy as np
    if edit.get("kind") != "compact_charge":
        raise RuntimeError("Unsupported offline preview edit")
    semitones = edit.get("pitch_semitones", 0)
    if not -12 <= semitones <= 0 or not .5 <= edit["duration_seconds"] <= 30:
        raise RuntimeError("Invalid charge edit")
    if semitones:
        speed = 2 ** (semitones / 12)
        positions = np.arange(int(np.floor((len(samples) - 1) / speed)) + 1) * speed
        original = np.arange(len(samples))
        samples = np.stack([np.interp(positions, original, samples[:, c])
                            for c in range(samples.shape[1])], axis=1)
    target = round(edit["duration_seconds"] * rate)
    count = min(round(rate * .08), target // 4)
    if target + count > len(samples):
        raise RuntimeError("Charge edit cannot lengthen the source")
    head_count = (target + count) // 2
    tail_count = target + count - head_count
    head, tail = samples[:head_count], samples[-tail_count:]
    theta = np.linspace(0, np.pi / 2, count)[:, None]
    join = head[-count:] * np.cos(theta) + tail[:count] * np.sin(theta)
    return np.concatenate((head[:-count], join, tail[count:]), axis=0)


def prepare(output, manifest, plan):
    import numpy as np
    import soundfile as sf
    (output / "previews").mkdir(exist_ok=True)
    clips = {}
    for item in manifest["clips"]:
        if item["status"] != "downloaded":
            continue
        source = output / item["source"]
        if digest(source) != item["source_sha256"]:
            raise RuntimeError("Paid source changed; refusing to replace it")
        samples, rate = sf.read(source, dtype="float64", always_2d=True)
        if rate != 44100 or not np.isfinite(samples).all():
            raise RuntimeError("Unexpected audio source format")
        samples -= np.mean(samples, axis=0)
        if item.get("preview_edit"):
            if item["loop"]:
                raise RuntimeError("Charge preview edits require a one-shot source")
            samples = edit_charge(samples, rate, item["preview_edit"])
        if item["loop"]:
            # Overlap the tail with the head, ending immediately before the new
            # start sample. This removes an arbitrary wrap discontinuity without
            # changing playback speed. Untouched originals remain available.
            count = min(round(rate * .1), len(samples) // 4)
            theta = np.linspace(0, np.pi / 2, count)[:, None]
            join = samples[-count:] * np.cos(theta) + samples[:count] * np.sin(theta)
            samples = np.concatenate((samples[count:-count], join), axis=0)
        peak = float(np.abs(samples).max())
        if peak < 1e-5:
            raise RuntimeError("Generated clip is effectively silent")
        target_dbfs = -3 + item.get("preview_gain_db", 0)
        gain = 10 ** (target_dbfs / 20) / peak
        samples *= gain
        if not item["loop"]:
            count = min(round(rate * .005), len(samples) // 2)
            ramp = np.linspace(0, 1, count)[:, None]
            samples[:count] *= ramp
            samples[-count:] *= ramp[::-1]
        preview = output / "previews" / (item["id"] + ".wav")
        item["preview"] = preview.relative_to(output).as_posix()
        item["measurements"] = write_audio(preview, samples, rate)
        item["processing"] = {"dc_removed": True, "constant_gain_db": float(20 * np.log10(gain)),
                              "target_peak_dbfs": target_dbfs,
                              "edge_fade_ms": 0 if item["loop"] else 5,
                              "loop_overlap_ms": 100 if item["loop"] else 0,
                              "pitch_speed_or_spectral_changes": bool(item.get("preview_edit")),
                              "offline_edit": item.get("preview_edit")}
        item["loop_boundary_step"] = float(np.max(np.abs(samples[0] - samples[-1]))) if item["loop"] else None
        clips[item["id"]] = samples
    # A clearly labelled editorial mix, unrelated to timing in the live simulator.
    rate = 44100
    mix = np.zeros((rate * 55, 2))
    events = [(0,"factory_ambience",.12),(0,"extraction_fan",.07),(0,"cabinet_hum",.05),
              (1,"roof_lights",.45),(5,"sled_arrival",.8),(11,"nir_enable",.6),
              (12.5,"robot_travel",.7),(14,"inspection_shutter",.7),
              (17,"sealant_dispense",.45),(20,"robot_precision",.3),
              (23,"panel_pickup",.8),(26,"panel_placement",.8),(29.5,"thermal_enable",.6),
              (31,"inspection_shutter",.7),(33,"pinch_clamp",.8),
              (35,"spot_welding",.9),(39,"weld_cooling",.7),
              (45,"gripper_release",.7),(47,"sled_departure",.8)]
    timeline = []
    for at, name, level in events:
        if name not in clips:
            continue
        samples = clips[name]
        if samples.shape[1] == 1:
            samples = np.repeat(samples, 2, axis=1)
        if at == 0:
            samples = np.tile(samples, (int(np.ceil(len(mix) / len(samples))), 1))[:len(mix)]
        start = round(at * rate)
        count = min(len(samples), len(mix) - start)
        mix[start:start+count] += level * samples[:count, :2]
        timeline.append({"seconds": at, "clip": name, "relative_gain": level})
    if clips:
        mix *= min(1, 10 ** (-2 / 20) / max(1e-12, np.abs(mix).max()))
        ramp = np.linspace(0, 1, rate)[:, None]
        mix[:rate] *= ramp
        mix[-rate:] *= ramp[::-1]
        manifest["montage"] = {"file":"process_preview.wav", "note":"Editorial 55-second mix; not live process timing", "timeline":timeline,
                               "measurements":write_audio(output / "process_preview.wav", mix, rate)}
    cards = []
    for category in dict.fromkeys(c["category"] for c in plan["clips"]):
        cards.append(f"<h2>{html.escape(category)}</h2><section>")
        for item in sorted(manifest["clips"], key=lambda c: not bool(c.get("revision_note"))):
            if item["category"] != category or "preview" not in item:
                continue
            repeated = " loop" if item["loop"] else ""
            comparison = ""
            if item.get("previous_preview"):
                comparison = (f'<details><summary>Compare previous take</summary><audio controls{repeated} '
                              f'preload="none" src="{html.escape(item["previous_preview"], quote=True)}"></audio></details>')
            revision = ""
            if item.get("revision_note"):
                revision = f'<p><strong>Reworked:</strong> {html.escape(item["revision_note"])}</p>'
            cards.append(f'<article><h3>{html.escape(item["label"])}</h3><p>{item["measurements"]["duration_seconds"]:.1f} s · '
                         f'{"Loop — repeats for audition" if item["loop"] else "One shot"}</p>'
                         + revision +
                         f'<audio controls{repeated} preload="none" src="{item["preview"]}"></audio>'
                         + comparison +
                         f'<details><summary>Generation prompt</summary><p>{html.escape(item["text"])}</p></details>'
                         f'<p><a href="{item["source"]}" download>Original MP3</a> · <a href="{item["preview"]}" download>Preview WAV</a></p></article>')
        cards.append("</section>")
    page = '''<!doctype html><html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Dark factory sound review</title><style>
:root{color-scheme:dark;font:16px system-ui;background:#111723;color:#e9eef8}body{max-width:1140px;margin:32px auto;padding:0 22px}h1{font-size:30px}h2{margin-top:35px}h3{font-size:18px;margin:0 0 9px}p{color:#b9c7da;line-height:1.6}section{display:grid;grid-template-columns:repeat(auto-fit,minmax(290px,1fr));gap:16px}article,.montage{background:#1c2635;border:1px solid #3d4c61;border-radius:12px;padding:20px}audio{width:100%}a{color:#a7c4ff}summary{cursor:pointer;color:#bccff1}details p{font-size:14px}</style>
<h1>Dark factory sound review</h1><p>ElevenLabs effects for review. Nothing here is connected to the Unreal demo yet. Previews use constant gain with 100 ms overlap joins on loops; selected robot/fan takes are intentionally quieter. Untouched originals are available below. This is not a final loudness mix. Camera mode/capture sounds are illustrative cues.</p><div class="montage"><h2 style="margin-top:0">Process sequence · 55 seconds</h2><p>Editorial sequence with quiet background layers. These timings do not represent the live process.</p><audio controls preload="metadata" src="process_preview.wav"></audio></div>'''
    page = page.replace('<h1>Dark factory sound review</h1>', '<h1>' + html.escape(plan['title']) + '</h1>')
    if any(c.get("revision_note") for c in manifest["clips"]):
        page += '<p>Reworked clips are marked below. Expand “Compare previous take” to audition the original beside the revision. Other effects are retained from the first pass.</p>'
    page += "".join(cards) + '''<p><a href="manifest.json">Generation provenance and measurements</a></p><script>document.querySelectorAll('audio').forEach(a=>a.addEventListener('play',()=>document.querySelectorAll('audio').forEach(b=>{if(a!==b)b.pause()})));</script></html>'''
    (output / "index.html").write_text(page, encoding="utf-8")
    save(output, manifest)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--plan", type=Path, default=ROOT / "configs/audio_review.json")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/audio/review-20261022")
    parser.add_argument("--key-file", type=Path)
    parser.add_argument("--reuse-from", type=Path, help="Reuse identical recorded requests from a prior private batch")
    parser.add_argument("--generate", action="store_true")
    parser.add_argument("--account-check", action="store_true")
    args = parser.parse_args()
    if args.account_check:
        print(json.dumps(subscription(read_key(args.key_file))))
        return
    plan = json.loads(args.plan.read_text(encoding="utf-8"))
    ids = [c["id"] for c in plan["clips"]]
    if len(ids) != len(set(ids)) or any(not re.fullmatch(r"[a-z0-9_]+", i) for i in ids):
        raise RuntimeError("Invalid or duplicate clip IDs")
    if any(len(c["text"]) > 450 or not .5 <= c["duration_seconds"] <= 30 for c in plan["clips"]):
        raise RuntimeError("Invalid sound duration or prompt length")
    if any(not -30 <= c.get("preview_gain_db", 0) <= 0 for c in plan["clips"]):
        raise RuntimeError("Preview attenuation must be between -30 and 0 dB")
    output = args.output.resolve()
    # Generated media belongs only in the ignored asset tree.
    if not output.is_relative_to(ROOT / "assets"):
        raise RuntimeError("Output must be inside the private assets directory")
    path = output / "manifest.json"
    if path.exists():
        manifest = json.loads(path.read_text(encoding="utf-8"))
        if manifest["plan_sha256"] != digest(args.plan):
            raise RuntimeError("Plan changed; retain prior sources and use a new review directory")
    else:
        if not args.generate:
            raise RuntimeError("No recorded source batch; use --generate explicitly")
        if output.exists():
            raise RuntimeError("Unrecognized existing output directory")
        output.mkdir(parents=True)
        (output / "source").mkdir()
        manifest = {"title":plan["title"],"created_utc":datetime.now(timezone.utc).isoformat(),
                    "plan_sha256":digest(args.plan),"clips":[], "usage":"Private audition; no Unreal integration",
                    "credit_policy":"Existing allowance only; no billing changes or automatic request retries"}
        save(output, manifest)
    previous = {}
    if args.reuse_from:
        prior_output = args.reuse_from.resolve()
        if not prior_output.is_relative_to(ROOT / "assets") or prior_output == output:
            raise RuntimeError("Reuse requires a different private asset batch")
        prior = json.loads((prior_output / "manifest.json").read_text(encoding="utf-8"))
        previous = {c["id"]: c for c in prior["clips"] if c["status"] == "downloaded"}
        for item in plan["clips"]:
            old = previous.get(item.get("source_clip_id", item["id"]))
            if old is None or any(c["id"] == item["id"] for c in manifest["clips"]):
                continue
            request = {"model_id":plan["model_id"],"prompt_influence":plan["prompt_influence"],
                       "text":item["text"],"duration_seconds":item["duration_seconds"],"loop":item["loop"]}
            if request != old["request"]:
                continue
            source = (prior_output / old["source"]).resolve()
            if not source.is_relative_to(prior_output) or digest(source) != old["source_sha256"]:
                raise RuntimeError("Prior source failed reuse validation")
            destination = output / "source" / (item["id"] + ".mp3")
            shutil.copyfile(source, destination)
            clip = {**item,"request":request,"status":"downloaded",
                "source":destination.relative_to(output).as_posix(),"source_sha256":digest(destination),
                "source_bytes":destination.stat().st_size,"response_headers":{},
                "reused_from":{"manifest":str(prior_output / "manifest.json"),"clip_id":old["id"],
                               "original_response_headers":old.get("response_headers",{})}}
            if item.get("revision_note") and previous.get(item["id"], {}).get("preview"):
                comparison = (prior_output / previous[item["id"]]["preview"]).resolve()
                if not comparison.is_relative_to(prior_output) or not comparison.is_file():
                    raise RuntimeError("Invalid comparison preview")
                clip["previous_preview"] = Path(os.path.relpath(comparison, output)).as_posix()
            manifest["clips"].append(clip)
        save(output, manifest)
    if args.generate:
        key = read_key(args.key_file)
        before = subscription(key)
        manifest.setdefault("subscription_before", before)
        remaining = before["character_limit"] - before["character_count"]
        # Verify existing allowance before the batch with a conservative reserve.
        outstanding = sum(c["duration_seconds"] for c in plan["clips"] if c["id"] not in {x["id"] for x in manifest["clips"]})
        if remaining < outstanding * 100 + 1000:
            raise RuntimeError("Insufficient existing allowance for this bounded review batch")
        save(output, manifest)
        for item in plan["clips"]:
            existing = next((c for c in manifest["clips"] if c["id"] == item["id"]), None)
            if existing:
                if existing["status"] != "downloaded":
                    raise RuntimeError("Unfinished prior request; refusing duplicate paid generation")
                continue
            request = {"model_id":plan["model_id"],"prompt_influence":plan["prompt_influence"],
                       "text":item["text"],"duration_seconds":item["duration_seconds"],"loop":item["loop"]}
            clip = {**item,"request":request,"status":"request_started","requested_utc":datetime.now(timezone.utc).isoformat()}
            if item["id"] in previous and previous[item["id"]].get("preview"):
                preview = (args.reuse_from / previous[item["id"]]["preview"]).resolve()
                if not preview.is_relative_to(args.reuse_from.resolve()) or not preview.is_file():
                    raise RuntimeError("Invalid comparison preview")
                clip["previous_preview"] = Path(os.path.relpath(preview, output)).as_posix()
            manifest["clips"].append(clip)
            save(output, manifest)
            print("Generating " + item["id"], flush=True)
            headers, body = api(key, "/v1/sound-generation?output_format=" + plan["output_format"], request)
            if len(body) < 1024 or "audio" not in headers.get("content-type", ""):
                raise RuntimeError("Unexpected audio response; retain request record")
            source = output / "source" / (item["id"] + ".mp3")
            source.write_bytes(body)
            clip.update(status="downloaded",source=source.relative_to(output).as_posix(),source_sha256=digest(source),
                        source_bytes=len(body),response_headers={k:headers[k] for k in ("character-cost","request-id","content-type") if k in headers})
            save(output, manifest)
            print(f'Downloaded {item["id"]}: {len(body)} bytes; credits {headers.get("character-cost", "unreported")}', flush=True)
        manifest["subscription_after"] = subscription(key)
        manifest["reported_credits"] = str(sum(Decimal(c["response_headers"].get("character-cost", "0")) for c in manifest["clips"]))
        save(output, manifest)
    prepare(output, manifest, plan)
    print(json.dumps({"clips":len(manifest["clips"]),"reported_credits":manifest.get("reported_credits"),"review":str(output / "index.html")}))


if __name__ == "__main__":
    try:
        main()
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
    except Exception:
        print("Audio preparation failed; inspect local files before retrying. Credentials withheld.", file=sys.stderr)
        sys.exit(1)
