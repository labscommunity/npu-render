"""Runtime paths for the private-prefix Weston build; no system loader changes."""

import os
from pathlib import Path


def environment(root: Path) -> dict[str, str]:
    result = os.environ.copy()
    prefix = root / "build/weston-install"
    directories = sorted({str(path.parent) for path in prefix.rglob("lib*.so*")})
    if result.get("LD_LIBRARY_PATH"):
        directories.append(result["LD_LIBRARY_PATH"])
    result["LD_LIBRARY_PATH"] = os.pathsep.join(directories)
    result.setdefault(
        "NPU_RENDER_NATIVE_LIBRARY", str(root / "build/native/libnpu_render_native.so")
    )
    return result
