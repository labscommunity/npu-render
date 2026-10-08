"""Build the isolated, start-disabled WDDM kernel laboratory; never install it."""

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from prepare_wdk import DEFAULT_ROOT, prepare


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/windows-kernel"))
    parser.add_argument("--wdk-root", type=Path, default=DEFAULT_ROOT)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("The x64 WDDM laboratory requires Windows and MSVC")
    prepare(args.wdk_root, kernel=True)
    subprocess.run(
        [
            "cmake",
            "-S",
            "native/windows/kmd",
            "-B",
            str(args.build_dir),
            "-A",
            "x64",
            f"-DNR_WDK_ROOT={args.wdk_root.resolve()}",
        ],
        check=True,
    )
    subprocess.run(["cmake", "--build", str(args.build_dir), "--config", "Release"], check=True)
    subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "-C", "Release", "--output-on-failure"],
        check=True,
    )
    binary = args.build_dir / "Release/nr_render_kmd.sys"
    report = {
        "schema": 1,
        "kind": "kernel-build-only",
        "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "start_enabled": False,
        "installation_performed": False,
        "signing_performed": False,
        "npu_execution_tested": False,
        "desktop_offload_tested": False,
    }
    (args.build_dir / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
