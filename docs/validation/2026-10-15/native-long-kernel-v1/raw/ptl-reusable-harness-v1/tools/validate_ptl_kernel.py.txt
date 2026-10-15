"""Qualify a pinned custom kernel on the narrow Panther Lake FP16 carrier.

No driver or display settings change. Proprietary compilers and graph binaries
are supplied locally and are never downloaded or redistributed by this tool.
"""

import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import time
import traceback
from datetime import UTC, datetime
from pathlib import Path

from npu_render.ptl_carrier import read_kernel_text, replace_identity_code

RUNTIME = "2026.4.1-22982-e213a147257-releases/2026/4"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def child(output, operation):
    report = {
        "started": datetime.now(UTC).isoformat(),
        "completed": False,
        "passed": False,
        "phases": [],
        "timing_scope": "host request through copied output; diagnostic only",
    }
    try:
        import numpy as np
        import openvino as ov

        if ov.get_version() != RUNTIME:
            raise RuntimeError("This carrier requires its pinned OpenVINO runtime")
        core = ov.Core()
        if "NPU" not in core.available_devices:
            raise RuntimeError("NPU is unavailable")
        report["openvino_version"] = ov.get_version()
        report["device_name"] = str(core.get_property("NPU", "FULL_DEVICE_NAME"))
        for phase, file in (
            ("original-before", "original.blob"),
            ("custom", "patched.blob"),
            ("original-after", "original.blob"),
        ):
            report["active_phase"] = phase
            write(output / "child-report.json", report)
            model = core.import_model((output / file).read_bytes(), "NPU")
            raw = model.get_property("EXECUTION_DEVICES")
            devices = [raw] if isinstance(raw, str) else list(raw)
            if devices not in (["NPU"], ["NPU.0"]):
                raise RuntimeError(f"Unexpected execution devices: {devices}")
            request = model.create_infer_request()
            checks = []
            for iteration in range(64):
                if iteration < 8:
                    values = (np.arange(16).reshape(1, 16) - 8 + iteration / 4).astype(np.float16)
                else:
                    values = (
                        np.random.default_rng(809 + iteration)
                        .uniform(-16, 16, (1, 16))
                        .astype(np.float16)
                    )
                if phase != "custom":
                    expected = (values + np.float16(1)).astype(np.float16)
                elif operation == "identity":
                    expected = values
                else:
                    reordered = values.reshape(2, 8).astype(np.float32)[:, [3, 0, 5, 2, 7, 4, 1, 6]]
                    expected = (
                        np.clip(reordered * np.float32(0.5) + np.float32(1), -2, 2)
                        .astype(np.float16)
                        .reshape(1, 16)
                    )
                began = time.perf_counter_ns()
                request.start_async({0: values})
                if not request.wait_for(10000):
                    raise RuntimeError("NPU inference deadline expired")
                observed = request.get_output_tensor(0).data.copy()
                elapsed = (time.perf_counter_ns() - began) / 1e6
                if (
                    observed.dtype != expected.dtype
                    or observed.shape != expected.shape
                    or not np.array_equal(observed, expected)
                ):
                    report["mismatch"] = {
                        "phase": phase,
                        "iteration": iteration,
                        "expected": expected.flatten().tolist(),
                        "observed": observed.flatten().tolist(),
                    }
                    raise RuntimeError("NPU result differs from the independent oracle")
                checks.append(
                    {
                        "iteration": iteration,
                        "elapsed_ms": elapsed,
                        "input_sha256": digest(values.tobytes()),
                        "output_sha256": digest(observed.tobytes()),
                    }
                )
            report["phases"].append(
                {
                    "phase": phase,
                    "passed": True,
                    "execution_devices": devices,
                    "exact_components": 1024,
                    "checks": checks,
                }
            )
            del request, model
        report["passed"] = True
    except Exception as error:
        report.update(error=repr(error), traceback=traceback.format_exc())
    finally:
        report.update(completed=True, finished=datetime.now(UTC).isoformat())
        write(output / "child-report.json", report)
    return 0 if report["passed"] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--carrier", type=Path)
    parser.add_argument("--kernel-elf", type=Path)
    parser.add_argument("--carrier-sha256")
    parser.add_argument("--kernel-sha256")
    parser.add_argument("--operation", choices=["identity", "indexed"], default="identity")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--execute", action="store_true", help="run the isolated NPU oracle")
    parser.add_argument("--child", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.child:
        return child(args.output, args.operation)
    for name in ("carrier", "kernel_elf", "carrier_sha256", "kernel_sha256"):
        if getattr(args, name) is None:
            parser.error(f"--{name.replace('_', '-')} is required")
    for checksum in (args.carrier_sha256, args.kernel_sha256):
        if not re.fullmatch(r"[0-9a-fA-F]{64}", checksum):
            parser.error("SHA-256 pins must contain 64 hexadecimal characters")
    args.output.mkdir(parents=True, exist_ok=False)
    report = {
        "started": datetime.now(UTC).isoformat(),
        "completed": False,
        "passed": False,
        "operation": args.operation,
        "hardware_execution_tested": False,
        "custom_kernel_execution_qualified": False,
        "desktop_changed": False,
        "driver_changed": False,
        "full_desktop_offload": False,
        "tahoma_tested": False,
    }
    process = None
    try:
        blob, elf = args.carrier.read_bytes(), args.kernel_elf.read_bytes()
        if digest(blob) != args.carrier_sha256.lower() or digest(elf) != args.kernel_sha256.lower():
            raise ValueError("Input bytes do not match their required SHA-256 pins")
        code = read_kernel_text(elf)
        modified, contract = replace_identity_code(blob, code)
        write(args.output / "carrier-contract.json", contract)
        report.update(
            carrier_sha256=digest(blob),
            kernel_elf_sha256=digest(elf),
            patched_sha256=digest(modified),
            replacement_text_bytes=len(code),
            non_code_bytes_preserved=True,
        )
        if args.execute:
            if os.name != "nt":
                raise RuntimeError("The qualified execution route requires Windows")
            (args.output / "original.blob").write_bytes(blob)
            (args.output / "patched.blob").write_bytes(modified)
            report["hardware_execution_tested"] = True
            command = [
                sys.executable,
                str(Path(__file__).resolve()),
                "--child",
                "--output",
                str(args.output.resolve()),
                "--operation",
                args.operation,
            ]
            with (
                (args.output / "child.stdout.log").open("xb") as stdout,
                (args.output / "child.stderr.log").open("xb") as stderr,
            ):
                process = subprocess.Popen(
                    command, stdout=stdout, stderr=stderr, creationflags=subprocess.CREATE_NO_WINDOW
                )
                report["owned_child_pid"] = process.pid
                write(args.output / "report.json", report)
                try:
                    process.wait(timeout=60)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                    raise RuntimeError("Owned qualification child exceeded 60 seconds") from None
            report["child_exit"] = process.returncode
            result_path = args.output / "child-report.json"
            result = json.loads(result_path.read_text()) if result_path.exists() else {}
            report["child_result"] = result
            if process.returncode or not result.get("completed") or not result.get("passed"):
                raise RuntimeError("Hardware qualification failed; see child evidence")
            report["custom_kernel_execution_qualified"] = True
        report["passed"] = True
    except Exception as error:
        report.update(error=repr(error), traceback=traceback.format_exc())
    finally:
        if process is not None and process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        report.update(completed=True, finished=datetime.now(UTC).isoformat())
        write(args.output / "report.json", report)
    print(json.dumps({key: value for key, value in report.items() if key != "child_result"}))
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
