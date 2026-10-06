"""Prepare the pinned Weston source with the restricted native renderer adapter."""

import argparse
import shutil
import subprocess
from pathlib import Path

REVISION = "00201e077f5fd8e754ad6888097da720e17dfa86"
MIRROR = "https://github.com/wayland-mirror/weston.git"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("build/weston-source"))
    args = parser.parse_args()
    source = args.source.resolve()
    project = Path(__file__).resolve().parents[1]
    if not source.exists():
        subprocess.run(
            ["git", "clone", "--filter=blob:none", "--no-checkout", MIRROR, source], check=True
        )
        subprocess.run(["git", "-C", source, "checkout", "--detach", REVISION], check=True)
    revision = subprocess.check_output(
        ["git", "-C", source, "rev-parse", "HEAD"], text=True
    ).strip()
    if revision != REVISION:
        raise RuntimeError(f"Weston checkout must be exactly {REVISION}")
    patch = project / "native/weston/weston.patch"
    applied = (
        subprocess.run(
            ["git", "-C", source, "apply", "--reverse", "--check", patch], capture_output=True
        ).returncode
        == 0
    )
    if not applied:
        # Recognize only our prior integer-only adapter patch. Reversing checked
        # hunks preserves unrelated checkout edits; unknown patches still fail.
        current = patch.read_text()
        legacy = current.replace("source_clip, ev->alpha", "source_clip != NULL, ev->alpha")
        upgrade = (
            subprocess.run(
                ["git", "-C", source, "apply", "--reverse", "--check", "-"],
                input=legacy,
                text=True,
                capture_output=True,
            ).returncode
            == 0
        )
        if upgrade:
            subprocess.run(
                ["git", "-C", source, "apply", "--reverse", "-"],
                input=legacy,
                text=True,
                check=True,
            )
        try:
            subprocess.run(["git", "-C", source, "apply", "--check", patch], check=True)
            subprocess.run(["git", "-C", source, "apply", patch], check=True)
        except subprocess.CalledProcessError:
            if upgrade:
                subprocess.run(
                    ["git", "-C", source, "apply", "-"], input=legacy, text=True, check=True
                )
            raise
    for name in ("npu-render-weston.c", "npu-render-weston.h"):
        shutil.copy2(project / "native/weston" / name, source / "libweston" / name)
    shutil.copy2(project / "native/include/npu_render.h", source / "libweston/npu_render.h")
    print(f"Prepared Weston {REVISION} at {source}")


if __name__ == "__main__":
    main()
