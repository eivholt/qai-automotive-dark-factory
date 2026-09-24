#!/usr/bin/env python3
"""Audit the proposed KV-only AR64 Ministral graph; compilation is unavailable.

The pinned SDK can serialize the unmodified loaded DLC, but its serializer exits
with code 1 while serializing the pruned graph, without a Python exception.
DlcModule.save instead silently retains the original graph. This helper therefore
records an in-memory graph audit only. It never creates a deployable bundle or
claims numerical parity, scheduler correctness or a speed gain.

Run in the isolated QAIRT 0.11 / SDK 2.50.40 Linux environment. A future candidate
needs a fresh source graph export, followed by quantization and runtime checks.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import inspect
import json
import os
from pathlib import Path
import struct


GRAPH = "SHA_26_ar64_cl4096_2_of_2"
KV_NAMES = {f"past_{kind}_{layer}_out" for layer in range(13, 26) for kind in ("key", "value")}
SCHEDULER_SOURCES = (
    "examples/Genie/Genie/src/qualla/engine.cpp",
    "examples/Genie/Genie/src/qualla/engines/qnn-htp.cpp",
    "examples/Genie/Genie/src/qualla/engines/qnn-htp/KVCache/kvmanager.cpp",
)
SERIALIZATION_FAILURE = (
    "SDK 2.50.40 IrDlcSerializer.serialize exits 1 on the pruned loaded graph "
    "without a Python exception; the unmodified graph serialization control passes. "
    "DlcModule.save does not persist changes to its returned IR graph. "
    "No compiled candidate was produced."
)


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value: object) -> None:
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")


def validate_source_snapshot(source: Path, provenance: dict) -> None:
    require(Path(provenance["source_export"]).resolve() == source.resolve(),
            "Supplied source export differs from preparation")
    require(sha256(source / "provenance.json") == provenance["source_provenance_sha256"],
            "Original export provenance changed")
    original = json.loads((source / "provenance.json").read_text())
    manifest = {row["path"]: row["sha256"] for row in original["export_files"]}
    for name in ("genie_config.json", "artifacts/backend_extensions.json",
                 "artifacts/source-tekken-tokenizer.json", "artifacts/split_model_1.bin"):
        require(name in manifest and sha256(source / "genie" / name) == manifest[name],
                f"Original export artifact changed: {name}")


def validate_outputs(outputs: list[dict], *, has_logits: bool) -> None:
    by_name = {row["name"]: row for row in outputs}
    expected = KV_NAMES | ({"logits"} if has_logits else set())
    require(len(by_name) == len(outputs) and set(by_name) == expected,
            "Unexpected final-split output inventory")
    for name in KV_NAMES:
        dims = [8, 1, 128, 64] if name.startswith("past_key") else [8, 1, 64, 128]
        require(by_name[name]["dimensions"] == dims and by_name[name]["dtype"].endswith("FLOAT_16"),
                f"Unexpected FP16 KV output: {name}")
    if has_logits:
        require(by_name["logits"]["dimensions"] == [1, 64, 131072]
                and by_name["logits"]["dtype"].endswith("FLOAT_16"), "Unexpected logits")


def validate_retained(before: dict, after: dict) -> None:
    require(bool(after) and set(after) < set(before), "Expected a strict subgraph")
    for name, value in after.items():
        require(before[name] == value, f"Retained tensor/encoding changed: {name}")
    require("model.lm_head.weight" not in after, "Unused output head remains")


def outputs(graph) -> list[dict]:
    return [{"name": t.name(), "dimensions": t.dims(), "dtype": str(t.data_type())}
            for t in graph.get_output_tensors_of_graph()]


def encoding_hash(tensor) -> str:
    enc = tensor.get_encoding()
    digest = hashlib.sha256(str((str(enc.type), str(enc.qnnDefinition), enc.overridden)).encode())
    kind = str(enc.type)
    if kind.endswith("UNDEFINED"):
        return digest.hexdigest()
    if kind.endswith("BLOCK"):
        detail = enc.bqEncInfo
        digest.update(str((detail.axis, detail.blockAxis, detail.blockSize)).encode())
        rows = detail.encInfos
    elif kind.endswith("AXIS_SCALE_OFFSET") or kind.endswith("BW_AXIS_SCALE_OFFSET"):
        detail = enc.axisEncInfo
        digest.update(str(detail.axis).encode())
        rows = detail.encInfos
    elif kind.endswith("SCALE_OFFSET") or kind.endswith("BW_SCALE_OFFSET"):
        rows = [enc.encInfo]
    else:
        raise ValueError(f"Unreviewed quantization encoding: {kind}")
    digest.update(struct.pack("<Q", len(rows)))
    for row in rows:
        digest.update(struct.pack("<Idddd??i", row.bw, row.scale, row.offset, row.min,
                                  row.max, row.is_fixed_point, row.is_symmetric, int(row.mapping)))
    return digest.hexdigest()


def fingerprints(graph) -> dict:
    """Hash original bytes and all exposed quantization parameters, without requantizing."""
    tensors = {t.name(): t for op in graph.get_ops() for t in op.inputs() + op.outputs()}
    result = {}
    for name, tensor in sorted(tensors.items()):
        row = {"dimensions": tensor.dims(), "dtype": str(tensor.data_type()),
               "axis": str(tensor.axis_format()), "encoding_sha256": encoding_hash(tensor)}
        if tensor.is_static_tensor():
            data = tensor.get_data()
            row["bytes"] = data.nbytes
            row["data_sha256"] = hashlib.sha256(memoryview(data).cast("B")).hexdigest()
        result[name] = row
    return result


def audit(source: Path, output: Path, sdk: Path, *, verify_tensors: bool,
          fingerprint_cache: Path | None) -> dict:
    from export_ministral_qairt import MODEL_SHA256, SDK_VERSION, SDK_FILES

    require(sdk.name == SDK_VERSION and importlib.metadata.version("qairt-dev") == "0.11.0",
            "Unexpected SDK or QAIRT package")
    for relative, expected in SDK_FILES.items():
        require(sha256(sdk / relative) == expected, f"SDK source changed: {relative}")
    os.environ["QAIRT_SDK_ROOT"] = str(sdk)
    import qairt  # Select the pinned SDK before loading its Python bindings.
    from qti.aisw.converters.common import ir_graph, modeltools

    require(not output.exists(), "Refusing to replace an existing audit")
    original = json.loads((source / "provenance.json").read_text())
    require(original["source_sha256"] == MODEL_SHA256 and original["sdk"] == SDK_VERSION,
            "Unrecognized source export")
    candidates = list((source / "cache").glob(f"*/{GRAPH}.dlc"))
    require(len(candidates) == 1, "Require one original AR64 final-split DLC")
    dlc = candidates[0]
    provenance = {"format": "ministral-kv-only-graph-audit-v1",
                  "status": "blocked_modified_ir_serialization", "compile_allowed": False,
                  "source_export": str(source), "source_provenance_sha256": sha256(source / "provenance.json"),
                  "source_gguf_sha256": MODEL_SHA256, "sdk": SDK_VERSION,
                  "script_sha256": sha256(Path(__file__)), "ar64_source": str(dlc),
                  "ar64_source_sha256": sha256(dlc), "serialization_failure": SERIALIZATION_FAILURE,
                  "retained_values_verified_in_memory": False, "serialized_values_verified": False}
    validate_source_snapshot(source, provenance)
    reader = modeltools.IrDlcReader(str(dlc), disableLazyWeightLoading=True)
    graph = reader.get_ir_graph()
    provenance["outputs_before"] = outputs(graph)
    validate_outputs(provenance["outputs_before"], has_logits=True)
    before = None
    if verify_tensors:
        verifier_sha = hashlib.sha256((inspect.getsource(fingerprints) + inspect.getsource(encoding_hash)).encode()).hexdigest()
        key = {"source_dlc_sha256": provenance["ar64_source_sha256"], "verifier_sha256": verifier_sha}
        cached = json.loads(fingerprint_cache.read_text()) if fingerprint_cache and fingerprint_cache.exists() else {}
        if cached.get("key") == key:
            before = cached["tensors"]
            require(hashlib.sha256(json.dumps(before, sort_keys=True).encode()).hexdigest()
                    == cached["tensors_sha256"], "Fingerprint cache contents changed")
        else:
            before = fingerprints(graph)
            if fingerprint_cache:
                fingerprint_cache.parent.mkdir(parents=True, exist_ok=True)
                write_json(fingerprint_cache, {"key": key, "tensors": before,
                           "tensors_sha256": hashlib.sha256(json.dumps(before, sort_keys=True).encode()).hexdigest()})
    before_ops = {op.name for op in graph.get_ops()}
    # The graph-level update updates its output registry; the tensor setter alone does not.
    graph.update_tensor_type("logits", ir_graph.IR_TENSOR_TYPE_NATIVE)
    graph.remove_disconnected_nodes(False)
    graph.validate_graph()
    provenance["outputs_after"] = outputs(graph)
    validate_outputs(provenance["outputs_after"], has_logits=False)
    provenance["ops_before"] = len(before_ops)
    provenance["ops_after"] = len(graph.get_ops())
    provenance["removed_ops"] = sorted(before_ops - {op.name for op in graph.get_ops()})
    require(provenance["ops_after"] < provenance["ops_before"], "The head was not removed")
    if before is not None:
        validate_retained(before, fingerprints(graph))
        provenance["retained_values_verified_in_memory"] = True
    require(sha256(dlc) == provenance["ar64_source_sha256"], "Original DLC changed")
    provenance["scheduler_sources_sha256"] = {name: sha256(sdk / name) for name in SCHEDULER_SOURCES}
    provenance["runtime_limits"] = [
        "AR64 logits-free plus AR1 logits disables Genie's newer scheduler.",
        "Legacy rewind resets KV; previous append_rewind evidence does not apply.",
        "Legacy final-step strategy needs 65/129-token boundary checks.",
        "Single-row AR64 logits is unsafe: the current scalar reader still offsets by n_process-1 rows.",
        "No numerical, runtime-cache or speed validation is established by this audit.",
    ]
    output.mkdir(parents=True)
    write_json(output / "graph-audit.json", provenance)
    return provenance


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-export", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--audit-only", action="store_true", help="Explicitly select the only supported mode.")
    parser.add_argument("--verify-retained-tensors", action="store_true",
                        help="Also compare all in-memory bytes/encodings; takes several minutes.")
    parser.add_argument("--fingerprint-cache", type=Path)
    parser.add_argument("--compile", action="store_true", help="Unavailable; fails before reading model data.")
    args = parser.parse_args()
    require(not args.compile, SERIALIZATION_FAILURE)
    source = args.source_export.resolve(strict=True)
    output = args.output.resolve()
    sdk = args.sdk_root.resolve(strict=True)
    require(output != source and not output.is_relative_to(source) and not source.is_relative_to(output),
            "Audit and original export must be separate directories")
    report = audit(source, output, sdk, verify_tensors=args.verify_retained_tensors,
                   fingerprint_cache=args.fingerprint_cache)
    print(json.dumps({"output": str(output), "status": report["status"]}), flush=True)


if __name__ == "__main__":
    main()
