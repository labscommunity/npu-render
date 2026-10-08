"""Qualify graphics-owned NPU textures; no desktop or performance claim."""

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate(records, width, height, rgba, control, sample, expected_error, debug):
    final = records[-1]
    if final["full_desktop_offload"]:
        raise ValueError("probe claimed desktop offload")
    cleanup = [r["alias_cleanup_failures"] for r in records if "alias_cleanup_failures" in r]
    if any(cleanup):
        raise ValueError("alias cleanup failed")
    if expected_error:
        if final["passed"] or expected_error not in final.get("error", ""):
            raise ValueError("expected rejection missing")
        if any(r.get("stage") == "npu_import_and_compile" for r in records):
            raise ValueError("rejected resource reached inference preparation")
        if "mapping is smaller" in expected_error and not cleanup:
            raise ValueError("mapping rejection cleanup evidence missing")
        return
    frames = [r for r in records if "frame" in r]
    if (
        not final["passed"]
        or final["execution_device"] != "NPU"
        or not final["npu_driver"]
        or final["frames"] != 16
        or [r["frame"] for r in frames] != list(range(16))
        or any(r["max_byte_error"] > 1 for r in frames)
        or final["max_byte_error"] > 1
        or (final["width"], final["height"]) != (width, height)
        or final["format"] != ("RGBA" if rgba else "BGRA")
        or final["host_input_copy_bytes"] != width * height * 4 * 2 * 16
        or final["host_output_copy_bytes"] != 0
        or not final["npu_output_shared"]
        or not final["host_synchronized"]
        or final["mapping_owner"] != "GPU"
        or final["graphics_debug_enabled"] != debug
        or final["graphics_debug_errors"]
        or final["gpu_validation_draws"] != (16 if sample else 0)
    ):
        raise ValueError("pixel, execution, dimensions or transfer contract failed")
    if control:
        if final["consumer_api"] != "D3D12_BUFFER" or final["mapped_alias"]:
            raise ValueError("buffer control scope mismatch")
    elif final["consumer_api"] != "D3D11_TEXTURE" or not final["mapped_alias"] or cleanup != [0]:
        raise ValueError("texture import or lifetime evidence missing")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--debug", action="store_true", help="require Windows Graphics Tools")
    args = parser.parse_args()
    if os.name != "nt":
        raise ValueError("Windows, Intel graphics and an Intel NPU are required")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("refusing to overwrite evidence")
    package = importlib.util.find_spec("openvino")
    if package is None or package.origin is None:
        raise ValueError("install the pinned OpenVINO environment")
    runtime = Path(package.origin).parent / "libs"
    binary = args.binary_dir.resolve() / "nr_d3d11_texture_interop.exe"
    sources = [
        "CMakeLists.txt",
        "tools/d3d11_texture_interop.cpp",
        "windows/typed_texture.hpp",
        "windows/blend_model.hpp",
        "windows/d3d12_buffers.hpp",
        "windows/ddi/contract.hpp",
    ]
    report = {
        "scope": "NPU writes a graphics-owned texture; native D3D11 reads pixels in one process",
        "full_desktop_offload": False,
        "started_utc": datetime.now(UTC).isoformat(),
        "binaries_sha256": {binary.name: sha256(binary)},
        "sources_sha256": {name: sha256(args.source_dir / name) for name in sources},
        "runtime_dlls_sha256": {p.name: sha256(p) for p in sorted(runtime.glob("*.dll"))},
        "supervisor_sha256": sha256(Path(__file__)),
        "runs": [],
        "passed": False,
    }
    env = os.environ.copy()
    env["PATH"] = str(runtime) + os.pathsep + env["PATH"]
    cases = [
        ("bgra-64", 64, 48, [], None),
        ("rgba-320", 320, 240, ["--rgba"], None),
        ("bgra-720p", 1280, 720, [], None),
        ("bgra-display", 2880, 1800, [], None),
        ("sample-bgra-64", 64, 48, ["--sample"], None),
        ("sample-rgba-320", 320, 240, ["--sample", "--rgba"], None),
        ("sample-bgra-720p", 1280, 720, ["--sample"], None),
        ("sample-bgra-display", 2880, 1800, ["--sample"], None),
        ("buffer-control", 64, 48, ["--buffer-control"], None),
        ("reject-width", 63, 48, [], "align to 256-byte rows"),
        ("reject-direct", 64, 48, ["--direct-import"], "blocked before device creation"),
        ("reject-ddi-nt", 64, 48, ["--ddi-import"], "blocked before device creation"),
        ("reject-npu-mapping", 64, 256, ["--diagnostic-npu-map"], "mapping is smaller"),
        ("reject-legacy", 64, 48, ["--diagnostic-legacy"], "CreateSharedResource"),
    ]
    output.mkdir(parents=True, exist_ok=True)
    for name, width, height, options, expected_error in cases:
        command = [str(binary), "--width", str(width), "--height", str(height), *options]
        if args.debug:
            command.append("--debug")
        entry = {"name": name, "command": command, "passed": False}
        started = time.perf_counter()
        stdout_path, stderr_path = output / f"{name}.jsonl", output / f"{name}.stderr.log"
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            try:
                process = subprocess.run(
                    command,
                    env=env,
                    stdout=stdout,
                    stderr=stderr,
                    timeout=180,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                entry["exit_code"] = process.returncode
                if process.returncode != (1 if expected_error else 0):
                    raise RuntimeError(f"unexpected exit code {process.returncode}")
                stdout.flush()
                records = [
                    json.loads(line)
                    for line in stdout_path.read_text().splitlines()
                    if line.strip()
                ]
                validate(
                    records,
                    width,
                    height,
                    "--rgba" in options,
                    "--buffer-control" in options,
                    "--sample" in options,
                    expected_error,
                    args.debug,
                )
                entry["passed"] = True
            except (
                OSError,
                ValueError,
                KeyError,
                IndexError,
                RuntimeError,
                subprocess.TimeoutExpired,
            ) as exc:
                entry["error"] = str(exc)
        entry["wall_seconds"] = time.perf_counter() - started
        report["runs"].append(entry)
        (output / "supervisor.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{name}: {'passed' if entry['passed'] else entry['error']}", flush=True)
    report["finished_utc"] = datetime.now(UTC).isoformat()
    report["passed"] = all(r["passed"] for r in report["runs"])
    report["captured_sha256"] = {
        p.name: sha256(p) for p in sorted(output.iterdir()) if p.name != "supervisor.json"
    }
    (output / "supervisor.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
