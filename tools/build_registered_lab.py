"""Clean-build and test the isolated registered UMD; never install or sign it."""

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
from pathlib import Path


def source_hashes():
    directory = Path("native/windows/registered_lab")
    paths = [
        p for p in directory.iterdir() if p.suffix in {".cpp", ".hpp"} or p.name == "CMakeLists.txt"
    ]
    paths.extend(
        [
            Path("native/windows/kmd/contract.hpp"),
            Path("native/windows/ddi/unsupported.inc"),
            Path("native/windows/ddi/contract.hpp"),
            Path("native/windows/blend_model.hpp"),
        ]
    )
    return {p.as_posix(): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(paths)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=Path("build/windows-registered"))
    parser.add_argument("--wdk-include-root", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        raise RuntimeError("The registered UMD laboratory requires Windows and MSVC")
    sources = source_hashes()
    package = importlib.util.find_spec("openvino")
    if package is None or package.origin is None:
        raise RuntimeError("install the locked OpenVINO environment first")
    runtime_root = Path(package.origin).parent
    environment = os.environ.copy()
    environment["PATH"] = str(runtime_root / "libs") + os.pathsep + environment["PATH"]
    subprocess.run(
        [
            "cmake",
            "-S",
            "native/windows/registered_lab",
            "-B",
            str(args.build_dir),
            "-A",
            "x64",
            f"-DNR_WDK_INCLUDE_ROOT={args.wdk_include_root.resolve()}",
            f"-DOpenVINO_DIR={runtime_root / 'cmake'}",
        ],
        check=True,
        env=environment,
    )
    # MSBuild reused a UMD after kernel_memory.hpp changed in a live lab build.
    # Source-file hashes alone cannot establish which headers produced an image.
    subprocess.run(
        ["cmake", "--build", str(args.build_dir), "--config", "Release", "--clean-first"],
        check=True,
        env=environment,
    )
    subprocess.run(
        ["ctest", "--test-dir", str(args.build_dir), "-C", "Release", "--output-on-failure"],
        check=True,
        env=environment,
    )
    if source_hashes() != sources:
        raise RuntimeError("Sources changed during the build; discard this result")
    binaries = {}
    for name in (
        "nr_fill_umd.dll",
        "nr_registered_clear_probe.exe",
        "nr_registered_lab_test.exe",
        "nr_registered_npu_worker.exe",
        "nr_registered_exchange_test.exe",
        "nr_registered_vertex_test.exe",
        "nr_registered_vertex_stream_probe.exe",
        "nr_registered_texture_test.exe",
        "nr_registered_raster_test.exe",
        "nr_registered_projection_test.exe",
        "nr_registered_output_merger_test.exe",
        "nr_registered_draw_replay_probe.exe",
        "nr_registered_uniform_draw_test.exe",
        "nr_registered_solid_rectangle_test.exe",
        "nr_registered_indexed_exchange_test.exe",
        "nr_registered_indexed_worker.exe",
        "nr_registered_indexed_stream_worker.exe",
        "nr_registered_indexed_stream_relay.exe",
        "nr_registered_texture_crop_test.exe",
        "nr_registered_point_texture_draw_test.exe",
        "nr_registered_linear_texture_draw_test.exe",
    ):
        binaries[name] = hashlib.sha256(
            (args.build_dir / "Release" / name).read_bytes()
        ).hexdigest()
    report = {
        "schema": 1,
        "kind": "registered-umd-build-only",
        "clean_build": True,
        "source_sha256": sources,
        "binary_sha256": binaries,
        "installation_performed": False,
        "signing_performed": False,
        "npu_execution_tested": False,
        "desktop_offload_tested": False,
    }
    (args.build_dir / "registered-build-report.json").write_text(
        json.dumps(report, indent=2) + "\n"
    )
    print(json.dumps(report))


if __name__ == "__main__":
    main()
