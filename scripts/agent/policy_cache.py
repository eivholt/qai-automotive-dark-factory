"""Persist only a model/runtime-bound static policy prefix, never a decision."""
import hashlib
import json
import copy
from pathlib import Path
import time
from urllib.request import urlopen

def policy_prefix(controller):
    # A template boundary marker, never evaluated as a tool observation.
    marker = "__DF_CACHE_BOUNDARY_09cba91e__"
    if marker in controller.prompt:
        raise ValueError("Boundary marker appears in policy")
    rendered = controller.post("/apply-template", {"messages": [
        {"role": "system", "content": controller.prompt}, {"role": "user", "content": marker}],
        "add_generation_prompt": True})["prompt"]
    if rendered.count(marker) != 1:
        raise ValueError("Template does not preserve the boundary marker")
    return rendered.split(marker)[0]


def warm(controller, prefix):
    # Some pinned servers emit one token even with n_predict=0. Discard the
    # entire response: this is cache preparation, never a process decision.
    return controller.post("/completion", {"prompt": prefix, "n_predict": 0,
        "cache_prompt": True, "return_tokens": True, "temperature": 0,
        "samplers": ["temperature"], "response_fields": ["tokens", "tokens_predicted", "timings"]})


def tool_prefix(controller, messages, code):
    """Only the already-selected action and next message envelope are known."""
    from single_token import ACTIONS
    if code not in ACTIONS:
        raise ValueError("Unknown selected action")
    marker = "__DF_PENDING_TOOL_45fd3cf__"
    if any(marker in message["content"] for message in messages):
        raise ValueError("Boundary marker appears in actual history")
    next_messages = copy.deepcopy(messages)
    next_messages.extend([{"role": "assistant", "content": code},
                          {"role": "user", "content": "Tool result: " + marker}])
    rendered = controller.post("/apply-template", {"messages": next_messages, "add_generation_prompt": True})["prompt"]
    if rendered.count(marker) != 1:
        raise ValueError("Template did not preserve unique pending-tool boundary")
    return rendered.split(marker)[0]


def file_hash(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def prepare(controller, directory):
    directory = Path(directory)
    identity = json.loads((directory / "identity.json").read_text())
    # Identity is written by the hash-pinned native launcher in the same private
    # slot-save directory. It includes exact runtime hashes and native arguments.
    if identity.get("schema") != "darkfactory-native-cache-identity-v1" or not identity.get("runtime_sha256"):
        raise ValueError("Invalid native cache identity")
    with urlopen(controller.endpoint + "/props", timeout=5) as response:
        props = json.load(response)
    if props.get("model_path") != identity["model_path"]:
        raise ValueError("Cache identity does not match the running model")
    prefix = policy_prefix(controller)
    binding = {"identity": identity, "prompt_sha256": controller.prompt_hash,
               "prefix_sha256": hashlib.sha256(prefix.encode()).hexdigest()}
    manifest_path = directory / "policy.json"
    cache_path = directory / "policy.bin"
    start = time.monotonic()
    result = {"source": "evaluated", "prompt_sha256": controller.prompt_hash, "decision_reused": False}
    try:
        manifest = json.loads(manifest_path.read_text())
        if manifest["binding"] == binding and manifest["cache_sha256"] == file_hash(cache_path):
            response = controller.post("/slots/0?action=restore", {"filename": "policy.bin"})
            if response.get("n_restored", 0) <= 0:
                raise ValueError("Empty policy cache restore")
            result.update(source="restored", backend=response)
    except (OSError, ValueError, KeyError, TypeError):
        pass  # A miss never changes the model input; reevaluate the real prefix.
    if result["source"] != "restored":
        response = warm(controller, prefix)
        result["discarded_setup_tokens"] = response.get("tokens_predicted", 0)
        saved = controller.post("/slots/0?action=save", {"filename": "policy.bin"})
        if saved.get("n_saved", 0) <= 0:
            raise ValueError("Empty policy cache save")
        manifest = {"binding": binding, "cache_sha256": file_hash(cache_path)}
        temporary = manifest_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(manifest, indent=2)+"\n")
        temporary.replace(manifest_path)
        result["backend"] = saved
    result["setup_ms"] = (time.monotonic()-start)*1000
    return result
