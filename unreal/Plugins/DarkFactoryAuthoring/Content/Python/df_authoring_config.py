"""Validated, source-controlled editor recipe. No Unreal dependency or runtime inputs."""
import json
import math
import os
from pathlib import Path
import tempfile

POSES = ("AwaitingPanel", "SealantApplied", "PanelPlaced")


def validate(value):
    if not isinstance(value, dict) or set(value) != {"schema_version", "pose", "viewport"}:
        raise ValueError("Expected only schema_version, pose and viewport")
    if type(value["schema_version"]) is not int or value["schema_version"] != 1:
        raise ValueError("Unsupported authoring schema")
    if value["pose"] not in POSES:
        raise ValueError(f"pose must be one of {POSES}")
    view = value["viewport"]
    if view is not None:
        if not isinstance(view, dict) or set(view) != {"location_cm", "rotation_deg"}:
            raise ValueError("viewport requires location_cm and rotation_deg")
        for key, limit in (("location_cm", 100000), ("rotation_deg", 360)):
            v = view[key]
            if not isinstance(v, list) or len(v) != 3 or any(
                type(x) not in (float, int) or not math.isfinite(x) or abs(x) > limit for x in v
            ):
                raise ValueError(f"Invalid {key}")
    return value


def load(path):
    return validate(json.loads(Path(path).read_text(encoding="utf-8-sig")))


def save(path, value):
    """Validate before replacing; never leave a partially written recipe."""
    encoded = json.dumps(validate(value), indent=2, allow_nan=False) + "\n"
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n",
                                         dir=path.parent, delete=False) as out:
            temporary = out.name
            out.write(encoded)
        os.replace(temporary, path)
    finally:
        if temporary and os.path.exists(temporary):
            os.unlink(temporary)
