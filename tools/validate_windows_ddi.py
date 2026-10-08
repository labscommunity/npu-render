"""Supervise isolated Direct3D DDI checks and retain exact binary/report evidence."""

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
    parser.add_argument("--devices", choices=["CPU", "GPU", "NPU"], nargs="+", default=["CPU"])
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if os.name != "nt":
        raise ValueError("Windows Direct3D runtime required")
    output = args.output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError("refusing to overwrite existing evidence")
    binary = args.binary_dir.resolve()
    files = [binary / "nr_ddi_probe.exe", binary / "nr_ddi_lab.dll"]
    hashes = {p.name: sha256(p) for p in files}
    runtime = args.runtime_dir
    if runtime is None:
        package = importlib.util.find_spec("openvino")
        if package is None or package.origin is None:
            raise ValueError("supply --runtime-dir or install the pinned OpenVINO environment")
        runtime = Path(package.origin).parent / "libs"
    if not (runtime / "openvino.dll").is_file():
        raise ValueError("OpenVINO runtime DLL missing")
    environment = os.environ.copy()
    environment["PATH"] = str(runtime.resolve()) + os.pathsep + environment["PATH"]
    output.mkdir(parents=True, exist_ok=True)
    report = {
        "scope": "process-local offscreen D3D11 DDI checks; no desktop routing",
        "started_utc": datetime.now(UTC).isoformat(),
        "binaries_sha256": hashes,
        "runtime_dlls_sha256": {p.name: sha256(p) for p in sorted(runtime.glob("*.dll"))},
        "runs": [],
        "passed": False,
    }
    for device in dict.fromkeys(args.devices):
        for rejection in ("", "reject-shader", "reject-shared", "reject-draw"):
            name = device.lower() + "-" + (rejection or "draw")
            result_path = output / f"{name}.json"
            command = [str(files[0]), str(files[1]), device, str(result_path)]
            if rejection:
                command.append(rejection)
            entry = {"name": name, "command": command, "passed": False}
            start = time.perf_counter()
            with (
                (output / f"{name}.stdout.log").open("wb") as stdout,
                (output / f"{name}.stderr.log").open("wb") as stderr,
            ):
                try:
                    completed = subprocess.run(
                        command, env=environment, stdout=stdout, stderr=stderr, timeout=180
                    )
                    entry["exit_code"] = completed.returncode
                    if completed.returncode != 0:
                        raise RuntimeError(f"probe exit code {completed.returncode}")
                    result = json.loads(result_path.read_text(encoding="utf-8"))
                    stats = result["stats"]
                    if not result["passed"] or result["requested_device"] != device:
                        raise RuntimeError("probe failed or mismatched backend")
                    if stats["device"] != device or result["device_creation_hresult"] != 0:
                        raise RuntimeError("device attribution failed")
                    if rejection:
                        if (
                            result["rejection_case"] != rejection
                            or not result["rejection_confirmed"]
                            or stats["executions"] != 0
                            or not stats["rejected"]
                        ):
                            raise RuntimeError("unsupported operation was not rejected")
                    elif (
                        result["cases"] != 8
                        or stats["executions"] != 8
                        or stats["draws"] != 8
                        or stats["rejected"]
                        or result["maximum_byte_error"] > 1
                    ):
                        raise RuntimeError("incomplete draw correctness/coverage check")
                    if device == "NPU" and not stats["driver"]:
                        raise RuntimeError("NPU driver attribution missing")
                    entry["passed"] = True
                except (
                    OSError,
                    ValueError,
                    KeyError,
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
