#!/usr/bin/env python3
"""Isolate scalar constants crossing INT16/FLOAT16 vision branches before QNN."""
import argparse
import copy
import json
import os
from pathlib import Path
import shutil


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    import onnx

    if args.output.exists():
        raise ValueError("Use a fresh checkpoint; source files must remain unchanged")
    encodings = json.loads((args.source / "vision_encoder.encodings").read_text())
    entries = {entry["name"]: entry for entry in encodings["activation_encodings"]}
    model = onnx.load(args.source / "vision_encoder.onnx")
    constants = {name: node for node in model.graph.node if node.op_type == "Constant" for name in node.output}
    initializers = {value.name: value for value in model.graph.initializer}
    nodes, changes = [], []
    for node in model.graph.node:
        if any(entries.get(name, {}).get("dtype") == "FLOAT" for name in node.output):
            for index, name in enumerate(node.input):
                if name not in constants and name not in initializers or entries.get(name, {}).get("dtype") != "INT":
                    continue
                isolated = name + "_fp16_" + node.name
                if name in constants:
                    duplicate = copy.deepcopy(constants[name])
                    duplicate.name = isolated + "_constant"
                    duplicate.output[0] = isolated
                    nodes.append(duplicate)
                else:
                    duplicate = copy.deepcopy(initializers[name])
                    duplicate.name = isolated
                    model.graph.initializer.append(duplicate)
                node.input[index] = isolated
                encodings["activation_encodings"].append({"name": isolated, "bw": 16, "dtype": "FLOAT", "enc_type": "PER_TENSOR"})
                changes.append({"node": node.name, "source": name, "isolated": isolated})
        nodes.append(node)
    if not changes:
        raise ValueError("No mixed-precision constants found")
    del model.graph.node[:]
    model.graph.node.extend(nodes)
    onnx.checker.check_model(model)
    shutil.copytree(args.source, args.output, copy_function=os.link)
    for filename in ("vision_encoder.onnx", "vision_encoder.encodings", "args.json"):
        (args.output / filename).unlink()
    onnx.save(model, args.output / "vision_encoder.onnx")
    (args.output / "vision_encoder.encodings").write_text(json.dumps(encodings, indent=2) + "\n")
    metadata = json.loads((args.source / "args.json").read_text())
    metadata["vision_mixed_precision"]["isolated_constants"] = changes
    (args.output / "args.json").write_text(json.dumps(metadata, indent=2) + "\n")
    print(json.dumps({"isolated_constants": changes, "output": str(args.output)}))


if __name__ == "__main__":
    main()
