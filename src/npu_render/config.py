"""Explicit backend policy and an atomic user-level selection file."""

import json
import os
import re
import tempfile
from pathlib import Path


def normalize_backend(value: str) -> str:
    target = value.upper()
    if not re.fullmatch(r"(?:CPU|GPU|NPU)(?:\.\d+(?:\.\d+)?)?", target):
        raise ValueError("backend must be CPU, GPU, NPU, or an explicit indexed device")
    return target


def config_path() -> Path:
    if custom := os.environ.get("NPU_RENDER_CONFIG"):
        return Path(custom).expanduser()
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData" / "Local"))
    else:
        base = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    return base / "npu-render" / "config.json"


def resolve_backend(explicit: str | None = None, path: Path | None = None) -> str:
    if explicit is not None:
        return normalize_backend(explicit)
    if "NPU_RENDER_BACKEND" in os.environ:
        return normalize_backend(os.environ["NPU_RENDER_BACKEND"])
    destination = path if path is not None else config_path()
    if not destination.exists():
        return "GPU"
    config = json.loads(destination.read_text(encoding="utf-8"))
    if not isinstance(config, dict) or config.get("version") != 1:
        raise ValueError(f"unsupported configuration in {destination}")
    if set(config) != {"version", "backend"} or not isinstance(config["backend"], str):
        raise ValueError(f"invalid configuration in {destination}")
    return normalize_backend(config["backend"])


def save_backend(backend: str, path: Path | None = None) -> Path:
    target = normalize_backend(backend)
    destination = path if path is not None else config_path()
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w", encoding="utf-8", dir=destination.parent, delete=False
        ) as stream:
            temporary = Path(stream.name)
            json.dump({"version": 1, "backend": target}, stream, indent=2)
            stream.write("\n")
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return destination
