"""Create a private Genie device-placement config without copying/changing weights.

Requires real platform inventory from probe_qnn_devices.cpp. A requested device
is not proof of execution placement; inspect native device handles at runtime.
"""
import argparse
import copy
import hashlib
import json
from pathlib import Path


def prepare(source, inventory, device_id, output):
    source, inventory, output = Path(source).resolve(), Path(inventory).resolve(), Path(output).resolve()
    config = json.loads(source.read_text())
    platform = json.loads(inventory.read_text())
    if platform.get("schema") != "qnn-platform-inventory-v1":
        raise ValueError("Require QNN runtime platform inventory")
    devices = [row for row in platform["devices"] if row["device_id"] == device_id]
    if len(devices) != 1:
        raise ValueError("Requested device is not uniquely present in runtime inventory")
    engine = config["dialog"]["engine"]
    if engine["backend"]["type"] != "QnnHtp" or engine["model"]["type"] != "binary":
        raise ValueError("Require a compiled QnnHtp Genie bundle")
    extension_source = (source.parent / engine["backend"]["extensions"]).resolve(strict=True)
    extensions = json.loads(extension_source.read_text())
    if len(extensions["devices"]) != 1:
        raise ValueError("Only a single-device source config is supported")
    selected = extensions["devices"][0]
    target = devices[0]
    if ("soc_model" in selected and selected["soc_model"] != target.get("soc_model")):
        raise ValueError("Requested device SoC does not match the compiled source config")
    if ("dsp_arch" in selected and selected["dsp_arch"] != f"v{target.get('arch')}"):
        raise ValueError("Requested device architecture does not match the compiled source config")
    cores = [row["core_id"] for row in selected["cores"]]
    available = [row["core_id"] for row in devices[0]["cores"]]
    if not cores or len(set(cores)) != len(cores) or not set(cores).issubset(available):
        raise ValueError("Source core selection is not available on requested device")
    original_extensions = copy.deepcopy(extensions)
    selected["device_id"] = device_id
    # Absolute references keep the source model, tokenizer and graph settings.
    config["dialog"]["tokenizer"]["path"] = str(
        (source.parent / config["dialog"]["tokenizer"]["path"]).resolve(strict=True))
    binary = engine["model"]["binary"]
    binary["ctx-bins"] = [str((source.parent / p).resolve(strict=True)) for p in binary["ctx-bins"]]
    if not binary["ctx-bins"]:
        raise ValueError("Bundle has no compiled contexts")
    engine["backend"]["extensions"] = str(output / "backend_extensions.json")
    output.mkdir(parents=True, exist_ok=False)
    def save(name, value):
        (output / name).write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")
    save("backend_extensions.json", extensions)
    save("genie_config.json", config)
    def identity(path):
        return {"path": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    save("placement.json", {"schema": "genie-device-placement-v1", "device_id": device_id,
        "core_ids": cores, "source_config": identity(source), "source_extensions": identity(extension_source),
        "source_device": original_extensions["devices"][0], "inventory": identity(inventory),
        "config": identity(output / "genie_config.json"),
        "extensions": identity(output / "backend_extensions.json"),
        "scope": "Requested placement only; runtime worker records context hashes; verify actual device handles",
        "weights_copied_or_modified": False})
    return output / "genie_config.json"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--bundle-config", type=Path, required=True)
    parser.add_argument("--platform-info", type=Path, required=True)
    parser.add_argument("--device-id", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New private directory; must not already exist")
    args = parser.parse_args()
    print(prepare(args.bundle_config, args.platform_info, args.device_id, args.output))


if __name__ == "__main__":
    main()
