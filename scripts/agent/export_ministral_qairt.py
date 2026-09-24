#!/usr/bin/env python3
"""Export the pinned Ministral Q4_K_M source with Tekken and runtime YaRN.

Run inside the isolated Linux QAIRT environment described in
docs/ministral_qairt_export.md. This is a model-specific experimental exporter;
successful compilation does not establish native/HTP decision parity.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import math
import os
from pathlib import Path
import resource
import subprocess
import time


MODEL_SHA256 = "9ed150d4367e68df0ac8e1540f6ddc65b42d0ee26378329d1ecbca60f93fc5f8"
TOKENIZER_SHA256 = "99cf274236c60277fcfad861a5a1007518687ad06ba8938760f50b55ffa0b1ef"
SDK_VERSION = "2.50.40.260831"
PACKAGES = {
    "qairt-dev": "0.11.0",
    "torch": "2.6.0+cpu",
    "numpy": "1.26.4",
    "transformers": "4.57.1",
    "gguf": "0.9.1",
    "tokenizers": "0.22.2",
}
SDK_FILES = {
    "lib/python/qti/aisw/converters/gguf_builder/modeling_gguf_pytorch_utils.py":
        "db0c2f072d0bbeb202e6030c933209b48827d7392fae17e49ad96de12bea988b",
    "lib/python/qti/aisw/converters/gguf_builder/ggml.py":
        "1b6b9051c05546cb8936e50dcce523b82c8c5c7468caad7879510b4b861f227f",
    "lib/python/qti/aisw/graph_gen/architectures/common.py":
        "87ddbd3f31a87141eec5f8e94cc15a47b35b16f4a1640a9addb0fc3d8fbf05eb",
    "lib/python/qti/aisw/graph_gen/architectures/llama/model.py":
        "f242ebe2a834a629643c4401704237ba1443b26105d7252892bf0fbbd115a817",
    "examples/Genie/Genie/src/qualla/engines/qnn-htp/nsp-model.cpp":
        "fbb70b575cadad9f59da203925b6c9e7dc4d985516fe5d79d54850d6c4a6c66f",
}
ROPE = {
    "type": "rope", "rope-dim": 64, "rope-theta": 1000000.0,
    "rope-scaling": {
        "rope-type": "yarnrope", "factor": 16.0, "dim": 128,
        "extrapolation-factor": 1.0,
        "attn-factor": 1.0 / (1.0 + 0.1 * math.log(16.0)),
        "beta-fast": 32.0, "beta-slow": 1.0,
        "original-max-position-embeddings": 16384,
    },
}


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


def prepare_tokenizer(official: Path, source_tokens: list[str], output: Path,
                      reference: Path) -> dict:
    from tokenizers import Tokenizer

    require(sha256(official) == TOKENIZER_SHA256, "Unrecognized official tokenizer")
    data = json.loads(official.read_text(encoding="utf-8"))
    vocab = data["model"]["vocab"]
    require(len(vocab) == len(source_tokens) == 131072, "Unexpected vocabulary size")
    by_id = {value: key for key, value in vocab.items()}
    changes = []
    # The official tokenizer retains reserved spellings where the source GGUF
    # names its thinking delimiters. Keep the exact source IDs and spellings.
    allowed = {34: ("<SPECIAL_34>", "[THINK]"), 35: ("<SPECIAL_35>", "[/THINK]")}
    for token_id, source in enumerate(source_tokens):
        current = by_id[token_id]
        if source == current:
            continue
        require(allowed.get(token_id) == (current, source),
                f"Unexpected tokenizer vocabulary difference at ID {token_id}")
        del vocab[current]
        vocab[source] = token_id
        for item in data["added_tokens"]:
            if item["id"] == token_id:
                item["content"] = source
        changes.append({"id": token_id, "official": current, "gguf": source})
    output.write_text(json.dumps(data, ensure_ascii=False, separators=(",", ":")),
                      encoding="utf-8")
    tokenizer = Tokenizer.from_file(str(output))
    references = json.loads(reference.read_text(encoding="utf-8"))
    rows = references["rows"]
    require(len(rows) >= 22, "Require all 22 native decision-prompt references")
    for index, row in enumerate(rows):
        tokens = tokenizer.encode(row["prompt"], add_special_tokens=False).ids
        require(tokens == row["tokens_without_bos"], f"Token mismatch in reference {index}")
        require([1] + tokens == row["tokens"], f"BOS mismatch in reference {index}")
        require(len(tokens) + 1 < 4096, f"Reference {index} exceeds context budget")
    for index in range(13):
        require(tokenizer.encode(chr(65 + index), add_special_tokens=False).ids == [1065 + index],
                "The A-M action token mapping changed")
    return {
        "official_tokenizer_sha256": TOKENIZER_SHA256,
        "source_tokenizer_sha256": sha256(output), "source_token_changes": changes,
        "native_prompt_rows_exact": len(rows), "prompt_reference_sha256": sha256(reference),
        "template_sha256": references["template_sha256"],
        "max_reference_tokens": max(len(row["tokens"]) for row in rows),
    }


def check_permutation() -> list[dict]:
    import numpy as np
    from transformers.modeling_gguf_pytorch_utils import reverse_permute_encodings, reverse_permute_weights

    rng = np.random.default_rng(20260919)
    results = []
    for heads in (32, 8):
        raw = rng.standard_normal((heads * 128, 9)).astype(np.float32)
        permuted = reverse_permute_weights(raw, 32, heads)
        expected = raw.reshape(heads, 64, 2, 9).transpose(0, 2, 1, 3).reshape(raw.shape)
        require(np.array_equal(permuted, expected), "SDK Q/K permutation changed")
        original = permuted.reshape(heads, 2, 64, 9).transpose(0, 2, 1, 3).reshape(raw.shape)
        require(np.array_equal(original, raw), "Q/K permutation roundtrip failed")
        scales = np.arange(heads * 128 * 3, dtype=np.float32).reshape(heads * 128, 3)
        encodings = reverse_permute_encodings({"scale": scales.flatten().tolist(),
                                              "offset": (-scales).flatten().tolist()},
                                             (heads * 128, 96), 32, heads)
        expected_scales = scales.reshape(heads, 64, 2, 3).transpose(0, 2, 1, 3).flatten()
        require(np.array_equal(encodings["scale"], expected_scales) and
                np.array_equal(encodings["offset"], -expected_scales),
                "Q/K block encodings do not follow the weight permutation")
        results.append({"heads": heads, "elements": raw.size, "roundtrip_exact": True,
                        "block_encodings_exact": True})
    return results


def inspect_binaries(root: Path, config: dict, sdk: Path) -> list[dict]:
    """Read compiled context metadata without executing any graph."""
    bundle = (root / "genie").resolve()
    binary_paths = config["dialog"]["engine"]["model"]["binary"]["ctx-bins"]
    require(len(binary_paths) == 2, "Expected two compiled context binaries")
    utility = sdk / "bin/x86_64-linux-clang/qnn-context-binary-utility"
    graphs = set()
    reports = []
    for index, name in enumerate(binary_paths):
        binary = (bundle / name).resolve(strict=True)
        require(binary.is_relative_to(bundle), "Exported binary escapes the bundle")
        report = root / f"split{index + 1}-binary-info.json"
        subprocess.run([str(utility), f"--context_binary={binary}", f"--json_file={report}"],
                       check=True, capture_output=True, text=True, timeout=120)
        info = json.loads(report.read_text(encoding="utf-8"))["info"]
        require(info["backendId"] == 6 and info["buildId"].startswith("v" + SDK_VERSION),
                "Unexpected compiled backend/version")
        require(info["numGraphs"] == 2, "Each split must contain AR1 and AR64")
        names = []
        for entry in info["graphs"]:
            graph = entry["info"]
            name = graph["graphName"]
            require(name not in graphs, "Duplicate graph across context binaries")
            graphs.add(name)
            names.append(name)
            rotary = {tensor["info"]["name"]: tensor["info"]
                      for tensor in graph["graphInputs"]
                      if tensor["info"]["name"] in ("position_ids_cos", "position_ids_sin")}
            require(len(rotary) == 2, "Graph must accept external rotary inputs")
            require(all(t["dimensions"][-1] == 64 and t["dataType"] == "QNN_DATATYPE_FLOAT_16"
                        for t in rotary.values()), "Unexpected rotary tensor format")
        reports.append({"binary": str(binary.relative_to(bundle)), "build_id": info["buildId"],
                        "backend_id": info["backendId"], "graphs": names,
                        "report_sha256": sha256(report)})
    expected = {f"SHA_26_ar{ar}_cl4096_{split}_of_2" for ar in (1, 64) for split in (1, 2)}
    require(graphs == expected, "Unexpected compiled graph inventory")
    return reports


def finalize_bundle(root: Path, provenance: dict, sdk: Path) -> None:
    config_path = root / "genie" / "genie_config.json"
    config = json.loads(config_path.read_text(encoding="utf-8"))
    require(config["dialog"]["context"]["size"] == 4096, "Unexpected runtime context")
    engine = config["dialog"]["engine"]
    require(engine["backend"]["type"] == "QnnHtp", "Unexpected runtime backend")
    extensions = (root / "genie" / engine["backend"]["extensions"]).resolve(strict=True)
    require(extensions.is_relative_to((root / "genie").resolve()), "Backend extensions escape bundle")
    stable_extensions = root / "genie" / "artifacts" / "backend_extensions.json"
    if extensions != stable_extensions.resolve():
        require(not stable_extensions.exists(), "Backend extensions destination already exists")
        extensions.rename(stable_extensions)
    engine["backend"]["extensions"] = "artifacts/backend_extensions.json"
    # MAD consumes external sin/cos tensors. Genie must generate the model's
    # exact YaRN schedule, which the Python configuration schema cannot express.
    engine["model"]["positional-encoding"] = ROPE
    write_json(config_path, config)
    tokenizer = (root / "genie" / config["dialog"]["tokenizer"]["path"]).resolve(strict=True)
    require(tokenizer.is_relative_to((root / "genie").resolve()), "Tokenizer escapes exported bundle")
    require(sha256(tokenizer) == provenance["source_tokenizer_sha256"], "Export changed tokenizer")
    provenance["compiled_binary_metadata"] = inspect_binaries(root, config, sdk)
    provenance["genie_config_sha256"] = sha256(config_path)
    provenance["export_files"] = [
        {"path": str(path.relative_to(root / "genie")),
         "size": path.stat().st_size, "sha256": sha256(path)}
        for path in sorted((root / "genie").rglob("*"))
        if path.is_file() and path.name not in ("export_provenance.json", "SHA256SUMS")
    ]
    write_json(root / "provenance.json", provenance)
    write_json(root / "genie" / "export_provenance.json", provenance)
    (root / "genie" / "SHA256SUMS").write_text(
        "".join(f"{sha256(path)}  {path.relative_to(root / 'genie')}\n"
                for path in sorted((root / "genie").rglob("*"))
                if path.is_file() and path.name != "SHA256SUMS"), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--gguf", type=Path, required=True)
    parser.add_argument("--official-tokenizer", type=Path, required=True)
    parser.add_argument("--prompt-reference", type=Path, required=True)
    parser.add_argument("--sdk-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--preflight-only", action="store_true")
    args = parser.parse_args()
    args.sdk_root = args.sdk_root.resolve(strict=True)
    args.output = args.output.resolve()
    require(args.sdk_root.name == SDK_VERSION, "Unexpected SDK version")
    require(sha256(args.gguf) == MODEL_SHA256, "Unrecognized source GGUF")
    packages = {name: importlib.metadata.version(name) for name in PACKAGES}
    require(packages == PACKAGES, f"Package versions differ from the validated stack: {packages}")
    for relative, expected in SDK_FILES.items():
        require(sha256(args.sdk_root / relative) == expected, f"SDK source changed: {relative}")
    require(not (args.output / "container").exists() and not (args.output / "genie").exists(),
            "Refusing to replace an existing export")
    args.output.mkdir(parents=True, exist_ok=True)
    temporary = args.output / "tmp"
    temporary.mkdir(exist_ok=True)
    # These must be set before importing QAIRT, whose import installs the SDK's
    # GGUF reader in transformers. Keep all build outputs in the private root.
    os.environ["QAIRT_SDK_ROOT"] = str(args.sdk_root)
    os.environ["QAIRT_TMP_DIR"] = str(temporary)
    from qairt.gen_ai_api.gen_ai_builder_factory import GenAIBuilderFactory
    from transformers.modeling_gguf_pytorch_utils import load_gguf_checkpoint

    import inspect
    loader_path = Path(inspect.getfile(load_gguf_checkpoint)).resolve()
    require(loader_path.is_relative_to(args.sdk_root), "GGUF loader came from a different SDK")
    metadata = load_gguf_checkpoint(str(args.gguf))
    model = metadata["config"]
    expected_shape = {"head_dim": 128, "hidden_size": 3072, "intermediate_size": 9216,
                      "num_attention_heads": 32, "num_key_value_heads": 8,
                      "num_hidden_layers": 26, "vocab_size": 131072}
    for key, expected in expected_shape.items():
        require(model.get(key) == expected, f"Unexpected {key}: {model.get(key)}")
    tokenizer_path = args.output / "source-tekken-tokenizer.json"
    provenance = {
        "source_gguf": str(args.gguf.resolve()), "source_sha256": MODEL_SHA256,
        "sdk": SDK_VERSION, "sdk_source_sha256": SDK_FILES, "packages": packages,
        "exporter_sha256": sha256(Path(__file__)), "loader_path": str(loader_path),
        "context_length": 4096, "target": "dsp_arch:v73;soc_model:77;cores:1",
        "attention_temperature": {"formula": "log(floor(pos/16384)+1)*0.1+1",
                                  "all_positions_0_to_4095": 1.0},
        "runtime_positional_encoding": ROPE, "qk_permutation": check_permutation(),
        "status": "preflight_passed_runtime_parity_unmeasured",
    }
    provenance.update(prepare_tokenizer(args.official_tokenizer, metadata["tokenizer"]["tokens"],
                                        tokenizer_path, args.prompt_reference))
    write_json(args.output / "preflight.json", provenance)
    print("PREFLIGHT_OK", flush=True)
    if args.preflight_only:
        return
    start = time.monotonic()
    builder = GenAIBuilderFactory.create(str(args.gguf), "HTP", cache_root=str(args.output / "cache"))
    builder.config.tokenizer_path = str(tokenizer_path)
    builder.set_targets([provenance["target"]])
    builder.set_transformation_options(options={"split.num_splits": 2})
    require(builder._transformation_config.model_transformer_config.arn_cl_options.context_length == [4096],
            "Unexpected compiler context lengths")
    require(builder.config.n_vocab == 131072, "Unexpected builder vocabulary size")
    container = builder.build()
    container.save(args.output / "container")
    container.export(args.output / "genie")
    provenance["elapsed_s"] = time.monotonic() - start
    provenance["max_rss_kb"] = resource.getrusage(resource.RUSAGE_SELF).ru_maxrss
    provenance["status"] = "compiled_runtime_parity_unmeasured"
    finalize_bundle(args.output, provenance, args.sdk_root)
    print(f"EXPORT_COMPLETE {args.output / 'genie' / 'genie_config.json'}", flush=True)


if __name__ == "__main__":
    main()
