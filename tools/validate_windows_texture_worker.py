"""Qualify native graphics/NPU textures across processes, including worker failures."""

import argparse
import ctypes
import importlib.util
import json
import os
import subprocess
import time
from datetime import UTC, datetime
from pathlib import Path

from validate_windows_texture_interop import sha256


def records(path):
    lines = path.read_bytes().splitlines()
    result = []
    for line in lines:
        try:
            result.append(json.loads(line))
        except (ValueError, UnicodeError):
            # A live producer may still be writing the last line.
            continue
    return result


def validate(items, width, height, rgba, reopen, debug):
    final = items[-1]
    frames = [r for r in items if "frame" in r]
    worker_frames = [r["worker_frame"] for r in items if "worker_frame" in r]
    identity = next(r for r in items if r.get("worker_ready"))
    worker_end = next(r for r in items if r.get("worker_passed"))
    launch = next(r for r in items if "worker_pid" in r)
    cleanup = [r["alias_cleanup_failures"] for r in items if "alias_cleanup_failures" in r]
    if (
        not final["passed"]
        or final["full_desktop_offload"]
        or not final["cross_process"]
        or final["native_texture_inputs"] != 2
        or final["native_texture_outputs"] != 1
        or final["host_pixel_upload_bytes"]
        or final["host_output_copy_bytes"]
        or final["host_constant_upload_bytes"] != 1024
        or not final["host_synchronized"]
        or final["gpu_producer_draws"] != 64
        or final["gpu_consumer_draws"] != 32
        or final["frames"] != 32
        or (final["width"], final["height"]) != (width, height)
        or final["format"] != ("RGBA" if rgba else "BGRA")
        or final["max_byte_error"] > 1
        or final["creator_reopened"] != reopen
        or final["graphics_debug_enabled"] != debug
        or final["graphics_debug_errors"]
        or [r["frame"] for r in frames] != list(range(32))
        or any(r["max_byte_error"] > 1 for r in frames)
        or worker_frames != list(range(32))
        or identity["execution_device"] != "NPU"
        or not identity["npu_driver"]
        or identity["host_pixel_upload_bytes"]
        or identity["host_output_copy_bytes"]
        or worker_end["worker_frames"] != 32
        or worker_end["worker_graphics_debug_errors"]
        or launch["parent_pid"] == launch["worker_pid"]
        or not launch["worker_job_kill_on_close"]
        or launch["inherited_handles"] != 6
        or cleanup != [0, 0, 0]
    ):
        raise ValueError("cross-process execution, pixels, transfer or lifetime contract failed")
    reopened = [
        r["creator_released_and_reopened"] for r in items if "creator_released_and_reopened" in r
    ]
    if reopened != ([3] if reopen else []):
        raise ValueError("creator release/reopen evidence mismatch")


def fault(process, stdout_path, target):
    """Kill only the owned parent or its reported child; retain the child's process handle."""
    from ctypes import wintypes

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
    kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.TerminateProcess.argtypes = (wintypes.HANDLE, wintypes.UINT)
    kernel.TerminateProcess.restype = wintypes.BOOL
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and process.poll() is None:
        items = records(stdout_path)
        if any(r.get("worker_ready") for r in items):
            launch = next(r for r in items if "worker_pid" in r)
            if launch["parent_pid"] != process.pid or launch["worker_pid"] == process.pid:
                raise ValueError("worker ownership evidence mismatch")
            child = kernel.OpenProcess(0x100001, False, launch["worker_pid"])
            if not child:
                raise OSError(ctypes.get_last_error(), "OpenProcess for owned worker")
            try:
                if kernel.WaitForSingleObject(child, 0) != 258:
                    raise ValueError("worker already exited before fault injection")
                started = time.monotonic()
                if target == "parent":
                    process.kill()
                elif not kernel.TerminateProcess(child, 77):
                    raise OSError(ctypes.get_last_error(), "TerminateProcess for owned worker")
                process.wait(timeout=15)
                if kernel.WaitForSingleObject(child, 10000) != 0:
                    raise ValueError("owned worker survived parent shutdown")
                return {
                    "target": target,
                    "worker_pid": launch["worker_pid"],
                    "parent_pid": process.pid,
                    "worker_exit_observed": True,
                    "injected_after_npu_ready": True,
                    "shutdown_seconds": time.monotonic() - started,
                    "completed_frames_before_injection": len([r for r in items if "frame" in r]),
                }
            finally:
                kernel.CloseHandle(child)
        time.sleep(0.005)
    raise RuntimeError("could not inject fault into a ready NPU worker")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary-dir", type=Path, required=True)
    parser.add_argument("--source-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--debug", action="store_true")
    args = parser.parse_args()
    if os.name != "nt":
        raise ValueError("Windows with Intel graphics and NPU is required")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("refusing to overwrite evidence")
    package = importlib.util.find_spec("openvino")
    if package is None or package.origin is None:
        raise ValueError("pinned OpenVINO environment is required")
    runtime = Path(package.origin).parent / "libs"
    binary = args.binary_dir.resolve() / "nr_texture_worker.exe"
    sources = [
        "CMakeLists.txt",
        "tools/texture_worker.cpp",
        "windows/typed_texture.hpp",
        "windows/blend_model.hpp",
        "windows/d3d12_buffers.hpp",
        "windows/ddi/contract.hpp",
    ]
    report = {
        "scope": "Native GPU producer, child NPU composition and native GPU consumer",
        "full_desktop_offload": False,
        "started_utc": datetime.now(UTC).isoformat(),
        "binaries_sha256": {binary.name: sha256(binary)},
        "sources_sha256": {name: sha256(args.source_dir / name) for name in sources},
        "supervisors_sha256": {
            p.name: sha256(p)
            for p in [
                Path(__file__),
                Path(__file__).with_name("validate_windows_texture_interop.py"),
            ]
        },
        "runtime_dlls_sha256": {p.name: sha256(p) for p in sorted(runtime.glob("*.dll"))},
        "runs": [],
        "passed": False,
    }
    env = os.environ.copy()
    env["PATH"] = str(runtime) + os.pathsep + env["PATH"]
    cases = [
        ("bgra-64", 64, 48, [], None),
        ("rgba-320-reopen", 320, 240, ["--rgba", "--reopen"], None),
        ("bgra-720p-reopen", 1280, 720, ["--reopen"], None),
        ("bgra-display-reopen", 2880, 1800, ["--reopen"], None),
        ("rgba-display-reopen", 2880, 1800, ["--rgba", "--reopen"], None),
        ("reject-width", 63, 48, [], "shape"),
        ("reject-worker-metadata", 64, 48, ["--reject-worker-shape"], "metadata"),
        ("parent-exit", 1280, 720, [], "parent"),
        ("worker-exit", 1280, 720, [], "worker"),
        ("recovery-after-faults", 1280, 720, ["--reopen"], None),
    ]
    output.mkdir(parents=True, exist_ok=True)
    for name, width, height, options, expected in cases:
        command = [str(binary), "--width", str(width), "--height", str(height), *options]
        if args.debug:
            command.append("--debug")
        entry = {"name": name, "command": command, "passed": False}
        started = time.perf_counter()
        stdout_path, stderr_path = output / f"{name}.jsonl", output / f"{name}.stderr.log"
        with stdout_path.open("xb") as stdout, stderr_path.open("xb") as stderr:
            process = subprocess.Popen(
                command,
                env=env,
                stdout=stdout,
                stderr=stderr,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            try:
                if expected in ("parent", "worker"):
                    entry["fault"] = fault(process, stdout_path, expected)
                else:
                    process.wait(timeout=180)
                entry["exit_code"] = process.returncode
                items = records(stdout_path)
                if expected in ("parent", "worker"):
                    if process.returncode == 0 or any(r.get("passed") for r in items):
                        raise ValueError("injected failure was reported as success")
                    if expected == "worker" and not any(
                        "worker exited before completion" in r.get("error", "") for r in items
                    ):
                        raise ValueError("worker failure did not reach parent")
                elif expected:
                    wanted = (
                        "unsupported texture shape"
                        if expected == "shape"
                        else "texture metadata mismatch"
                    )
                    if process.returncode != 1 or not any(
                        wanted in r.get("error", "") for r in items
                    ):
                        raise ValueError("expected explicit rejection missing")
                    if any(r.get("worker_ready") for r in items):
                        raise ValueError("invalid texture reached NPU execution readiness")
                else:
                    if process.returncode:
                        raise ValueError(f"unexpected exit code {process.returncode}")
                    validate(
                        items, width, height, "--rgba" in options, "--reopen" in options, args.debug
                    )
                entry["passed"] = True
            except (
                OSError,
                ValueError,
                RuntimeError,
                KeyError,
                IndexError,
                StopIteration,
                subprocess.TimeoutExpired,
            ) as exc:
                entry["error"] = str(exc)
            finally:
                if process.poll() is None:
                    process.kill()  # The job closes with the parent, terminating its child.
                    process.wait(timeout=15)
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
