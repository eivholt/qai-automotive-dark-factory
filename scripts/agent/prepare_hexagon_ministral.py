"""Prepare/verify a private Q4_0 decoder + CPU Q8_0 head candidate; never infer.

Prefer the publisher's BF16 GGUF. An explicit official Q8_0 source is a separate
requantized fallback. Neither candidate is the deployed Q4_K_M model. The helper
records source, quantizer and output hashes and checks model/tokenizer metadata.
"""
from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tarfile
import time

SCHEMA = "darkfactory-ministral-hexagon-candidate-v1"
REPO = "mistralai/Ministral-3-3B-Instruct-2512-GGUF"
REVISION = "eb599d408350ea2bb60452cb86be7c7b2fc28227"
RUNTIME_REVISION = "0eadefebd3f8f92a86d634a0e5b8fffc9dc792c0"
# Official GitHub/codeload tar.gz for the immutable revision, verified 2026-09-19.
RUNTIME_ARCHIVE_SHA256 = "19e38c290e4e97784c77e393e4b60593c84ff7c9c5c376d16dea6d459172e0fd"
SOURCE_SPECS = {
    "bf16": {"file": "Ministral-3-3B-Instruct-2512-BF16.gguf", "bytes": 6866745504,
             "sha256": "17ef932bea952e007f9dad63151da5699132ec513d1033d618df7382e24aa3ee"},
    "q8_0": {"file": "Ministral-3-3B-Instruct-2512-Q8_0.gguf", "bytes": 3652204704,
             "sha256": "8c2b72eb5861304fcfd5e82f1eddd6efa4115737f4239fd216a028a8852413ef"},
}
CANDIDATE_NAME = "Ministral-3-3B-Instruct-2512-decoder-Q4_0-head-Q8_0.gguf"


def sha_file(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_runtime_source(source, archive=None):
    """Verify Git or build_evk_llama.sh's extracted official source archive."""
    source = Path(source).resolve()
    if (source / ".git").exists():
        revision = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.run(["git", "-C", str(source), "diff", "--quiet", "HEAD"], check=False).returncode
        if revision != RUNTIME_REVISION or dirty:
            raise ValueError("Native runtime Git source differs from the pinned clean revision")
        return {"method": "git", "revision": revision, "tracked_edits": False}
    archive = Path(archive) if archive else source.with_suffix(source.suffix + ".tar.gz")
    if not archive.is_file() or sha_file(archive) != RUNTIME_ARCHIVE_SHA256:
        raise ValueError("Native runtime requires the hash-verified official pinned source archive")
    checked = 0
    prefix = "llama.cpp-" + RUNTIME_REVISION + "/"
    with tarfile.open(archive, "r:gz") as package:
        for member in package:
            if member.isdir():
                continue
            if not member.name.startswith(prefix):
                raise ValueError("Unexpected runtime archive root")
            relative = member.name[len(prefix):]
            path = source / relative
            if not path.resolve().is_relative_to(source):
                raise ValueError("Runtime archive member escapes its source directory")
            if member.issym():
                if not path.is_symlink() or str(path.readlink()) != member.linkname:
                    raise ValueError("Runtime source symlink differs: " + relative)
            elif member.isfile():
                if not path.is_file() or path.is_symlink() or path.stat().st_size != member.size:
                    raise ValueError("Runtime source file differs: " + relative)
                expected = hashlib.sha256()
                with package.extractfile(member) as stream:
                    for block in iter(lambda: stream.read(1024 * 1024), b""):
                        expected.update(block)
                if sha_file(path) != expected.hexdigest():
                    raise ValueError("Runtime source content differs: " + relative)
            else:
                raise ValueError("Unsupported runtime archive member")
            checked += 1
    if not checked:
        raise ValueError("Runtime source archive has no tracked files")
    return {"method": "official_archive_members", "revision": RUNTIME_REVISION,
            "archive_sha256": RUNTIME_ARCHIVE_SHA256, "tracked_members_verified": checked}


def gguf_summary(path):
    """Inspect only metadata/tensor headers; never load tensors into a runtime."""
    with Path(path).open("rb") as stream:
        def read(fmt):
            size = struct.calcsize("<" + fmt)
            data = stream.read(size)
            if len(data) != size:
                raise ValueError("Truncated GGUF")
            return struct.unpack("<" + fmt, data)[0]
        def string():
            size = read("Q")
            if size > 16 * 1024 * 1024:
                raise ValueError("Unexpected GGUF string size")
            data = stream.read(size)
            if len(data) != size:
                raise ValueError("Truncated GGUF string")
            return data.decode("utf-8")
        def value(kind):
            if kind == 8:
                return string()
            if kind == 9:
                subtype, count = read("I"), read("Q")
                if count > 1000000 or subtype == 9:
                    raise ValueError("Unexpected GGUF metadata array")
                for _ in range(count):
                    value(subtype)
                return None
            formats = {0: "B", 1: "b", 2: "H", 3: "h", 4: "I", 5: "i", 6: "f", 7: "?",
                       10: "Q", 11: "q", 12: "d"}
            if kind not in formats:
                raise ValueError("Unknown GGUF metadata type")
            return read(formats[kind])
        if stream.read(4) != b"GGUF" or read("I") != 3:
            raise ValueError("Expected GGUF v3")
        tensor_count, field_count = read("Q"), read("Q")
        if tensor_count > 10000 or field_count > 10000:
            raise ValueError("Unexpected GGUF header counts")
        metadata, preserved = {}, {}
        for _ in range(field_count):
            key = string()
            if key in metadata:
                raise ValueError("Duplicate metadata key")
            start = stream.tell()
            metadata[key] = value(read("I"))
            end = stream.tell()
            if key.startswith(("tokenizer.", "mistral3.")) or key == "general.architecture":
                stream.seek(start)
                preserved[key] = hashlib.sha256(stream.read(end - start)).hexdigest()
                stream.seek(end)
        tensors = {}
        for _ in range(tensor_count):
            name, dimensions = string(), read("I")
            if not 1 <= dimensions <= 4 or name in tensors:
                raise ValueError("Invalid GGUF tensor header")
            shape = [read("Q") for _ in range(dimensions)]
            tensor_type, offset = read("I"), read("Q")
            tensors[name] = {"shape": shape, "type": tensor_type}
        return {"architecture": metadata.get("general.architecture"),
                "block_count": metadata.get("mistral3.block_count"),
                "preserved_metadata_sha256": hashlib.sha256(json.dumps(preserved, sort_keys=True).encode()).hexdigest(),
                "tensor_types": dict(Counter(str(t["type"]) for t in tensors.values())), "tensors": tensors}


def validate_model(summary, candidate=False):
    tensors = summary["tensors"]
    if summary["architecture"] != "mistral3" or summary["block_count"] != 26 or len(tensors) != 236:
        raise ValueError("Expected the exact 26-layer Ministral 3B architecture")
    if tensors.get("token_embd.weight", {}).get("shape") != [3072, 131072] or "output.weight" in tensors:
        raise ValueError("Expected the recorded tied embedding/head")
    if candidate:
        for name, tensor in tensors.items():
            expected = 8 if name == "token_embd.weight" else (0 if len(tensor["shape"]) == 1 else 2)
            if tensor["type"] != expected:
                raise ValueError("Candidate must contain Q4_0 decoder, Q8_0 head and F32 norms only")


def source_file(root, kind, supplied=None, download=False):
    spec = SOURCE_SPECS[kind]
    path = Path(supplied).resolve() if supplied else root / "source" / spec["file"]
    if not path.exists():
        if supplied or not download:
            raise ValueError("Source missing; supply an official source or explicitly enable --download-source")
        path.parent.mkdir(parents=True, exist_ok=True)
        partial = path.with_suffix(path.suffix + ".part")
        if not partial.exists() or partial.stat().st_size != spec["bytes"]:
            url = f"https://huggingface.co/{REPO}/resolve/{REVISION}/{spec['file']}"
            subprocess.run(["curl", "--fail", "--location", "--retry", "2", "--connect-timeout", "20",
                            "--max-time", "900", "--continue-at", "-", "--output", str(partial), url],
                           check=True, timeout=2800)
        if partial.stat().st_size != spec["bytes"] or sha_file(partial) != spec["sha256"]:
            raise ValueError("Official source download hash/size mismatch; partial retained for investigation")
        partial.rename(path)
    if path.stat().st_size != spec["bytes"] or sha_file(path) != spec["sha256"]:
        raise ValueError("Source differs from the pinned official GGUF")
    return path


def quantizer_arguments(quantizer, source, target, kind, threads):
    arguments = [str(quantizer), "--pure", "--token-embedding-type", "q8_0", "--output-tensor-type", "q8_0"]
    if kind == "q8_0":
        arguments.append("--allow-requantize")
    return [*arguments, str(source), str(target), "Q4_0", str(threads)]


def verify(root):
    root = Path(root).resolve()
    report = json.loads((root / "candidate.json").read_text(encoding="utf-8"))
    candidate = root / CANDIDATE_NAME
    kind = report.get("source", {}).get("kind")
    if (report.get("schema") != SCHEMA or kind not in SOURCE_SPECS
            or report["source"]["sha256"] != SOURCE_SPECS[kind]["sha256"]
            or report.get("same_as_deployed_q4_k_m") is not False
            or report.get("runtime_revision") != RUNTIME_REVISION
            or report.get("target_quantization") != "Q4_0 decoder; Q8_0 tied embedding/head on CPU"
            or report.get("file") != CANDIDATE_NAME or sha_file(candidate) != report["sha256"]):
        raise ValueError("Candidate provenance or model hash mismatch")
    summary = gguf_summary(candidate)
    validate_model(summary, candidate=True)
    if (summary != report["output_summary"] or summary["preserved_metadata_sha256"] !=
            report["source_summary"]["preserved_metadata_sha256"]):
        raise ValueError("Candidate metadata or tensor inventory mismatch")
    return report


def prepare(args):
    root = args.root.resolve()
    root.mkdir(parents=True, exist_ok=True)
    if (root / "candidate.json").exists():
        report = verify(root)
        if report["source"]["kind"] != args.source_kind:
            raise ValueError("Existing candidate has different source precision; use a new directory")
        return report
    target = root / CANDIDATE_NAME
    partial = target.with_suffix(".gguf.partial")
    if target.exists() or partial.exists():
        raise ValueError("Candidate output already exists without completed provenance; use a new directory")
    source = source_file(root, args.source_kind, args.source, args.download_source)
    summary = gguf_summary(source)
    validate_model(summary)
    quantizer = args.quantizer.resolve()
    source_repo = args.quantizer_source.resolve()
    revision = subprocess.check_output(["git", "-C", str(source_repo), "rev-parse", "HEAD"], text=True).strip()
    if subprocess.run(["git", "-C", str(source_repo), "diff", "--quiet", "HEAD"], check=False).returncode:
        raise ValueError("Quantizer source has tracked edits; use a clean recorded revision")
    binaries = [quantizer, *sorted(quantizer.parent.glob("*.so*"))]
    runtime_hashes = {path.name: sha_file(path) for path in binaries if path.is_file()}
    arguments = quantizer_arguments(quantizer, source, partial, args.source_kind, args.threads)
    started = time.monotonic()
    with (root / "quantize.log").open("wb") as log:
        subprocess.run(arguments, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=1800)
    output_summary = gguf_summary(partial)
    validate_model(output_summary, candidate=True)
    if output_summary["preserved_metadata_sha256"] != summary["preserved_metadata_sha256"]:
        raise ValueError("Quantizer altered architecture/tokenizer metadata")
    if {k: v["shape"] for k, v in output_summary["tensors"].items()} != {k: v["shape"] for k, v in summary["tensors"].items()}:
        raise ValueError("Quantizer altered tensor names or dimensions")
    report = {"schema": SCHEMA, "file": CANDIDATE_NAME, "sha256": sha_file(partial),
              "bytes": partial.stat().st_size, "source": {"kind": args.source_kind, "repo": REPO,
              "revision": REVISION, **SOURCE_SPECS[args.source_kind]}, "source_path": str(source),
              "source_summary": summary, "output_summary": output_summary, "quantizer_source_revision": revision,
              "quantizer_binary_sha256": runtime_hashes, "command": arguments,
              "quantization_seconds": time.monotonic() - started, "runtime_revision": RUNTIME_REVISION,
              "target_quantization": "Q4_0 decoder; Q8_0 tied embedding/head on CPU",
              "requantized_source": args.source_kind == "q8_0", "same_as_deployed_q4_k_m": False,
              "inference_validated": False, "new_held_out_accuracy": False,
              "scope": "Isolated experimental candidate; requires decision and actual NPU offload validation"}
    partial.rename(target)
    (root / "candidate.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, required=True, help="Private candidate directory")
    p.add_argument("--source-kind", choices=SOURCE_SPECS, default="bf16")
    p.add_argument("--source", type=Path)
    p.add_argument("--download-source", action="store_true")
    p.add_argument("--quantizer", type=Path)
    p.add_argument("--quantizer-source", type=Path)
    p.add_argument("--threads", type=int, default=8)
    p.add_argument("--verify-only", action="store_true", help="Hash/header checks only; no runtime or inference")
    p.add_argument("--runtime-source", type=Path, help="Additionally verify pinned Git/archive source contents")
    p.add_argument("--runtime-archive", type=Path, help="Official source tar.gz used by build_evk_llama.sh")
    args = p.parse_args()
    if not args.verify_only and (not args.quantizer or not args.quantizer_source or not 1 <= args.threads <= 64):
        p.error("Preparation requires --quantizer, --quantizer-source and threads 1..64")
    report = verify(args.root) if args.verify_only else prepare(args)
    result = {k: report[k] for k in ("schema", "file", "sha256", "bytes", "target_quantization",
                                    "same_as_deployed_q4_k_m", "inference_validated")}
    if args.runtime_source:
        result["runtime_source"] = verify_runtime_source(args.runtime_source, args.runtime_archive)
    elif args.runtime_archive:
        p.error("--runtime-archive requires --runtime-source")
    print(json.dumps(result))


if __name__ == "__main__":
    main()
