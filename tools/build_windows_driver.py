"""Build the isolated WDDM kernel laboratory; never sign or install it."""

import argparse
import hashlib
import json
import os
import subprocess
from pathlib import Path

from prepare_wdk import DEFAULT_ROOT, prepare


def source_hashes():
    paths = sorted(
        path
        for path in Path("native/windows/kmd").iterdir()
        if path.suffix in {".cpp", ".hpp", ".h"} or path.name == "CMakeLists.txt"
    )
    return {path.as_posix(): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/windows-kernel"))
    parser.add_argument("--wdk-root", type=Path, default=DEFAULT_ROOT)
    parser.add_argument(
        "--lab-sync-probe",
        action="store_true",
        help="Observe the public synchronization callback; adapter startup stays disabled",
    )
    parser.add_argument(
        "--lab-worker-probe",
        action="store_true",
        help="Exercise owned paging memory, a kernel thread and a DPC; startup stays disabled",
    )
    parser.add_argument(
        "--lab-engine-probe",
        action="store_true",
        help="Exercise the paging engine with lab observers; never send test fences to Windows",
    )
    parser.add_argument(
        "--lab-paging-adapter",
        action="store_true",
        help="Enable paging-only startup for a disposable kernel-debug VM; UMD stays unready",
    )
    parser.add_argument(
        "--lab-aperture-probe",
        action="store_true",
        help="Check owned system-page translations in a disposable VM; startup stays disabled",
    )
    args = parser.parse_args()
    if args.lab_aperture_probe and (
        args.lab_sync_probe
        or args.lab_worker_probe
        or args.lab_engine_probe
        or args.lab_paging_adapter
    ):
        parser.error("The aperture diagnostic must run in isolation")
    if args.lab_paging_adapter and (
        args.lab_sync_probe or args.lab_worker_probe or args.lab_engine_probe
    ):
        parser.error("The paging adapter cannot be combined with synthetic startup probes")
    if os.name != "nt":
        raise RuntimeError("The x64 WDDM laboratory requires Windows and MSVC")
    prepare(args.wdk_root, kernel=True)
    sources = source_hashes()
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
            f"-DNR_KMD_LAB_SYNC_PROBE={'ON' if args.lab_sync_probe else 'OFF'}",
            f"-DNR_KMD_LAB_WORKER_PROBE={'ON' if args.lab_worker_probe else 'OFF'}",
            f"-DNR_KMD_LAB_ENGINE_PROBE={'ON' if args.lab_engine_probe else 'OFF'}",
            f"-DNR_KMD_LAB_PAGING_ADAPTER={'ON' if args.lab_paging_adapter else 'OFF'}",
            f"-DNR_KMD_LAB_APERTURE_PROBE={'ON' if args.lab_aperture_probe else 'OFF'}",
        ],
        check=True,
    )
    # MSBuild's kernel project did not reliably invalidate objects after changes
    # to included implementation headers during remote builds. Rebuild this
    # small target so the hashed image always comes from the current sources.
    subprocess.run(
        ["cmake", "--build", str(args.build_dir), "--config", "Release", "--clean-first"],
        check=True,
    )
    subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "-C", "Release", "--output-on-failure"],
        check=True,
    )
    binary = args.build_dir / "Release/nr_render_kmd.sys"
    symbols = binary.with_suffix(".pdb")
    if not symbols.is_file():
        raise RuntimeError("Kernel build did not produce matching debugger symbols")
    if source_hashes() != sources:
        raise RuntimeError("Kernel sources changed during the build; discard this result")
    report = {
        "schema": 1,
        "kind": "kernel-build-only",
        "sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "pdb_sha256": hashlib.sha256(symbols.read_bytes()).hexdigest(),
        "source_sha256": sources,
        "clean_build": True,
        "start_enabled": args.lab_paging_adapter,
        "rendering_ready": False,
        "lab_paging_adapter_enabled": args.lab_paging_adapter,
        "lab_fill_command_enabled": args.lab_paging_adapter,
        "lab_sync_probe_enabled": args.lab_sync_probe,
        "lab_worker_probe_enabled": args.lab_worker_probe,
        "lab_engine_probe_enabled": args.lab_engine_probe,
        "lab_aperture_probe_enabled": args.lab_aperture_probe,
        "installation_performed": False,
        "signing_performed": False,
        "npu_execution_tested": False,
        "desktop_offload_tested": False,
    }
    (args.build_dir / "build-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report))


if __name__ == "__main__":
    main()
