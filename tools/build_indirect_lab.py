"""Clean-build the disposable-guest indirect-display admission diagnostic only."""

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from prepare_wdk import DEFAULT_ROOT, prepare


def source_hashes():
    paths = sorted(Path("native/windows/indirect_lab").glob("*"))
    paths += [
        Path("native/windows/registered_lab/contract.hpp"),
        Path("native/windows/kmd/contract.hpp"),
    ]
    return {p.as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/windows-indirect-lab"))
    parser.add_argument("--wdk-root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument("--system-renderer-control", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("Windows and MSVC required")
    prepare(args.wdk_root, umdf=True)
    sources = source_hashes()
    subprocess.run(
        [
            "cmake",
            "-S",
            "native/windows/indirect_lab",
            "-B",
            str(args.build_dir),
            "-A",
            "x64",
            f"-DNR_WDK_ROOT={args.wdk_root.resolve()}",
            f"-DNR_IDD_SYSTEM_RENDERER_CONTROL={'ON' if args.system_renderer_control else 'OFF'}",
        ],
        check=True,
    )
    subprocess.run(
        [
            "cmake",
            "--build",
            str(args.build_dir),
            "--config",
            "Release",
            "--clean-first",
        ],
        check=True,
    )
    subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "-C", "Release", "--output-on-failure"],
        check=True,
    )
    if source_hashes() != sources:
        raise RuntimeError("Sources changed during build")
    report = {
        "schema": 1,
        "kind": "indirect-display-admission-build-only",
        "clean_build": True,
        "system_renderer_control": args.system_renderer_control,
        "source_sha256": sources,
        "sha256": hashlib.sha256(
            (args.build_dir / "Release/nr_indirect_lab.dll").read_bytes()
        ).hexdigest(),
        "binary_sha256": {
            name: hashlib.sha256((args.build_dir / "Release" / name).read_bytes()).hexdigest()
            for name in (
                "nr_indirect_lab.dll",
                "nr_indirect_topology_probe.exe",
                "nr_indirect_topology_gui.exe",
                "nr_indirect_consumer_test.exe",
                "nr_indirect_frame_readback_test.exe",
                "nr_indirect_lab.pdb",
            )
        },
        "installation_performed": False,
        "desktop_offload_tested": False,
    }
    (args.build_dir / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
