#!/usr/bin/env python3
"""Compare every pixel/token against the pinned processor, without loading a VLM.

Run on the target with qairt_processor_probe, its matching runtime libraries and
one or more prepared QAIRT validation manifests. Reports and images stay private.
"""
import argparse
from contextlib import suppress
import hashlib
import json
from pathlib import Path
import queue
import statistics
import subprocess
import threading


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(args):
    if args.output.exists():
        raise ValueError("Use a new private output report")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    report = {"schema": "qairt-fixed-processor-equivalence-v1", "model_inference": False,
              "probe_sha256": digest(args.probe), "rows": [], "complete": False}
    with args.output.with_suffix(".log").open("w", encoding="utf-8") as log:
        process = subprocess.Popen([str(args.probe.resolve()), str(args.bundle.resolve())],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log, text=True, bufsize=1)
        replies = queue.Queue()

        def read():
            for line in process.stdout:
                try:
                    replies.put(json.loads(line))
                except ValueError:
                    log.write(line)
            replies.put(None)

        reader = threading.Thread(target=read, daemon=True)
        reader.start()

        def receive():
            value = replies.get(timeout=args.timeout)
            if not isinstance(value, dict):
                raise RuntimeError("Processor probe exited")
            return value

        def save():
            args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")

        try:
            ready = receive()
            if ready.get("schema") != "fixed-processor-probe-v1" or ready.get("ready") is not True:
                raise ValueError("Unexpected processor probe")
            for manifest_path in args.manifest:
                manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
                if manifest.get("schema") != "automotive-qairt-validation-v1":
                    raise ValueError("Expected a prepared QAIRT validation manifest")
                for row in manifest["rows"]:
                    images = [(manifest_path.parent / image["path"]).resolve() for image in row["images"]]
                    if not 1 <= len(images) <= 3:
                        raise ValueError("Expected one to three sensor images")
                    if [digest(path) for path in images] != [image["sha256"] for image in row["images"]]:
                        raise ValueError("Validation image hash mismatch")
                    index = len(report["rows"])
                    text = ("<|im_start|>user\n" + "\n".join(["<__image__>"] * len(images)) +
                            row["prompt"] + "<|im_end|>\n<|im_start|>assistant\n")
                    # Labels, sample identity and metadata never enter the processor request.
                    request = {"request_id": str(index), "images": list(map(str, images)),
                               "formatted_text": text, "candidate_first": index % 2 == 0}
                    process.stdin.write(json.dumps(request) + "\n")
                    process.stdin.flush()
                    result = receive()
                    if result.get("request_id") != str(index):
                        raise ValueError("Mismatched processor response")
                    report["rows"].append(dict(result, sample=row["sample_id"],
                                              manifest_sha256=digest(manifest_path)))
                    save()
                    if not all(result.get(key) is True for key in ("same_pixels", "same_tokens", "same_grid")):
                        raise ValueError("Processor differs from reference: " + str(result))
            if not report["rows"]:
                raise ValueError("No validation rows")
            report["complete"] = True
            report["medians"] = {key: statistics.median(row[key] for row in report["rows"])
                                 for key in ("reference_ms", "candidate_ms")}
            save()
            print(json.dumps({"batches": len(report["rows"]), **report["medians"]}))
        finally:
            with suppress(OSError):
                process.stdin.close()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)
            reader.join(timeout=2)
            process.stdout.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("probe", "bundle", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--manifest", type=Path, nargs="+", required=True)
    parser.add_argument("--timeout", type=float, default=30)
    run(parser.parse_args())


if __name__ == "__main__":
    main()
