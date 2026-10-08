"""Validate experimental cross-process Direct3D/NPU textures, without desktop routing."""

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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--runtime-dir", type=Path)
    parser.add_argument("--source-dir", type=Path, required=True, help="native source directory")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument(
        "--gpu-readback",
        action="store_true",
        help="require native GPU texture consumption (currently fails)",
    )
    args = parser.parse_args()
    if os.name != "nt":
        raise ValueError("Windows and a working Intel NPU are required")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("refusing to overwrite existing evidence")
    binary = args.binary_dir.resolve()
    files = [binary / "nr_ddi_shared_probe.exe", binary / "nr_ddi_shared_lab.dll"]
    runtime = args.runtime_dir
    if runtime is None:
        package = importlib.util.find_spec("openvino")
        if package is None or package.origin is None:
            raise ValueError("supply --runtime-dir or install the pinned OpenVINO environment")
        runtime = Path(package.origin).parent / "libs"
    if not (runtime / "openvino.dll").is_file():
        raise ValueError("OpenVINO runtime DLL missing")
    source_names = [
        "CMakeLists.txt",
        "windows/blend_model.hpp",
        "windows/ddi/contract.hpp",
        "windows/ddi/driver.cpp",
        "windows/ddi/software_thunks.cpp",
        "windows/ddi/shared_memory.hpp",
        "windows/ddi/shared_probe.cpp",
        "windows/ddi/exports.def",
        "windows/ddi/unsupported.inc",
    ]
    report = {
        "scope": "explicit D3D11 shared textures; host event synchronization; no desktop routing",
        "full_desktop_offload": False,
        "gpu_readback_requested": args.gpu_readback,
        "started_utc": datetime.now(UTC).isoformat(),
        "binaries_sha256": {p.name: sha256(p) for p in files},
        "sources_sha256": {name: sha256(args.source_dir / name) for name in source_names},
        "supervisor_sha256": sha256(Path(__file__)),
        "runtime_dlls_sha256": {p.name: sha256(p) for p in sorted(runtime.glob("*.dll"))},
        "runs": [],
        "passed": False,
    }
    environment = os.environ.copy()
    environment["PATH"] = str(runtime.resolve()) + os.pathsep + environment["PATH"]
    environment.pop("NR_DDI_TRACE", None)
    cases = [
        ("bgra-64", ["64", "48", "16", "BGRA"]),
        ("rgba-320", ["320", "240", "16", "RGBA"]),
        ("bgra-720p", ["1280", "720", "16", "BGRA"]),
        ("bgra-display", ["2880", "1800", "16", "BGRA"]),
        ("reject-width", ["--reject-width"]),
    ]
    if args.gpu_readback:
        # Large native-GPU imports crashed the probe in retained controls. Do not
        # repeat that known invalid resource path against the graphics driver.
        cases = [case for case in cases if case[0] in {"bgra-64", "rgba-320", "reject-width"}]
    output.mkdir(parents=True, exist_ok=True)
    for name, options in cases:
        command = [str(files[0]), str(files[1]), *options]
        if args.gpu_readback and name != "reject-width":
            command.append("--gpu-readback")
        entry = {"name": name, "command": command, "passed": False}
        start = time.perf_counter()
        stdout_path, stderr_path = output / f"{name}.jsonl", output / f"{name}.stderr.log"
        with stdout_path.open("wb") as stdout, stderr_path.open("wb") as stderr:
            try:
                completed = subprocess.run(
                    command,
                    env=environment,
                    stdout=stdout,
                    stderr=stderr,
                    timeout=180,
                    creationflags=subprocess.CREATE_NO_WINDOW,
                )
                entry["exit_code"] = completed.returncode
                if completed.returncode != 0:
                    raise RuntimeError(f"probe exit code {completed.returncode}")
                stdout.flush()
                records = [
                    json.loads(line)
                    for line in stdout_path.read_text().splitlines()
                    if line.strip()
                ]
                final = records[-1]
                if not final["passed"] or final["full_desktop_offload"]:
                    raise RuntimeError("probe failed or claimed desktop offload")
                resources = [r for r in records if "resource_role" in r]
                if not resources or any(
                    r["live_surfaces"] or r["live_aliases"] or r["cleanup_failures"]
                    for r in resources
                ):
                    raise RuntimeError("incomplete resource cleanup")
                if name == "reject-width":
                    if not final["rejection_confirmed"]:
                        raise RuntimeError("unsupported dimensions were accepted")
                else:
                    width, height, frames = map(int, options[:3])
                    if final["frames"] != frames or final["maximum_byte_error"] > 1:
                        raise RuntimeError("pixel oracle or frame coverage failed")
                    if final["gpu_readback_requested"] != args.gpu_readback:
                        raise RuntimeError("GPU readback scope mismatch")
                    if args.gpu_readback and (
                        final["gpu_readback_frames"] != frames
                        or final["gpu_maximum_byte_error"] > 1
                    ):
                        raise RuntimeError("GPU texture consumption failed")
                    expected_format = 87 if options[3] == "BGRA" else 28
                    if records[0] != {
                        "width": width,
                        "height": height,
                        "requested_frames": frames,
                        "format": expected_format,
                    }:
                        raise RuntimeError("probe dimensions or format mismatch")
                    workers = [r for r in records if r.get("role") == "worker"]
                    if len(workers) != 1:
                        raise RuntimeError("worker attribution missing")
                    worker = workers[0]
                    if (
                        worker["device"] != "NPU"
                        or not worker["driver"]
                        or worker["executions"] != frames
                        or worker["draws"] != frames
                        or worker["rejected"]
                        or worker["inference_input_bytes"]
                        or worker["host_upload_bytes"]
                        or worker["host_copy_bytes"]
                        or worker["inference_output_bytes"] != width * height * 4 * frames
                    ):
                        raise RuntimeError("worker execution/copy attribution failed")
                    if not any(r.get("creator_released_and_reopened") for r in records):
                        raise RuntimeError("creator release and reopen not exercised")
                    if {r["resource_role"] for r in resources} != {
                        "parent-before-reopen",
                        "parent",
                        "worker",
                    }:
                        raise RuntimeError("resource lifecycle evidence missing")
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
        entry["wall_seconds"] = time.perf_counter() - start
        report["runs"].append(entry)
        (output / "supervisor.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{name}: {'passed' if entry['passed'] else entry['error']}", flush=True)
    report["finished_utc"] = datetime.now(UTC).isoformat()
    report["passed"] = all(run["passed"] for run in report["runs"])
    report["captured_sha256"] = {
        p.name: sha256(p) for p in sorted(output.iterdir()) if p.name != "supervisor.json"
    }
    (output / "supervisor.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
