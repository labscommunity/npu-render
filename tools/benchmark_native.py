"""Counterbalance native D3D12/NPU/CPU windows against a real inference server."""

import argparse
import ctypes
import hashlib
import importlib.util
import json
import math
import os
import subprocess
import tempfile
import time
from pathlib import Path

from npu_render.benchmark import comparisons, trial_order, write_report
from npu_render.inference import InferenceClient
from npu_render.live import distribution
from npu_render.openvino_diagnostics import inspect_stack, require_npu_driver
from npu_render.windows_session import inspect_session, require_interactive_desktop


def power_status():
    class Status(ctypes.Structure):
        _fields_ = [
            ("ac_line_status", ctypes.c_ubyte),
            ("battery_flag", ctypes.c_ubyte),
            ("battery_life_percent", ctypes.c_ubyte),
            ("system_status_flag", ctypes.c_ubyte),
            ("battery_life_seconds", ctypes.c_uint32),
            ("battery_full_life_seconds", ctypes.c_uint32),
        ]

    status = Status()
    api = ctypes.WinDLL("kernel32", use_last_error=True).GetSystemPowerStatus
    api.argtypes, api.restype = [ctypes.POINTER(Status)], ctypes.c_int
    if not api(ctypes.byref(status)):
        return {"error": ctypes.get_last_error()}
    return {name: getattr(status, name) for name, _ in status._fields_}


def parse_conditions(value):
    conditions = [item.strip().upper() for item in value.split(",")]
    if not conditions or len(set(conditions)) != len(conditions):
        raise ValueError("conditions must be a nonempty unique list")
    if set(conditions) - {"GPU", "NPU", "NPU_CONV", "CPU", "IDLE"}:
        raise ValueError("conditions must be GPU,NPU,NPU_CONV,CPU,IDLE")
    return conditions


def condition_backend(condition):
    return "NPU" if condition == "NPU_CONV" else condition


def native_comparisons(trials):
    result = comparisons(trials)
    for numerator, denominator in (("NPU_CONV", "NPU"), ("NPU_CONV", "GPU"), ("GPU", "IDLE")):
        ratios = []
        for index in sorted({trial["round"] for trial in trials}):
            group = {t["condition"]: t for t in trials if t["round"] == index}
            if not {numerator, denominator}.issubset(group):
                continue
            rates = {}
            for condition in (numerator, denominator):
                values = [r["tokens_per_second_end_to_end"] for r in group[condition]["inference"]]
                if values and all(value is not None and value > 0 for value in values):
                    rates[condition] = distribution(values)["median"]
            if len(rates) == 2:
                ratios.append({"round": index, "ratio": rates[numerator] / rates[denominator]})
        result[f"{numerator.lower()}_over_{denominator.lower()}"] = {
            "paired_ratios": ratios,
            "ratio_distribution": distribution([row["ratio"] for row in ratios]),
        }
    return result


def run(args):
    if os.name != "nt":
        raise RuntimeError("the native window requires Windows and Intel NPU hardware")
    if (
        args.rounds < 1
        or args.requests < 1
        or not math.isfinite(args.warmup_seconds)
        or not 0 <= args.warmup_seconds <= 3600
        or not args.executable.is_file()
    ):
        raise ValueError(
            "positive rounds/requests, warmup 0..3600s and a built executable are required"
        )
    conditions = parse_conditions(args.conditions)
    package = importlib.util.find_spec("openvino")
    environment = os.environ.copy()
    environment["PATH"] = (
        str(Path(package.origin).parent / "libs") + os.pathsep + environment["PATH"]
    )
    client = InferenceClient(args.endpoint, args.model, args.prompt, args.max_tokens, args.timeout)
    report = {
        "schema_version": 1,
        "status": "running",
        "renderer": "native shared-buffer D3D12 compute / NPU graph / integer CPU",
        "renderer_sha256": hashlib.sha256(args.executable.read_bytes()).hexdigest(),
        "model": args.model,
        "prompt_sha256": hashlib.sha256(args.prompt.encode()).hexdigest(),
        "inference_device_declared": args.inference_device,
        "inference_device_verified_by_harness": False,
        "max_tokens": args.max_tokens,
        "requests_per_trial": args.requests,
        "conditions": conditions,
        "settle_seconds": 2,
        "warmup_seconds_requested": args.warmup_seconds,
        "trials": [],
        "notes": [
            "One BGRA8 foreground surface; differs from the Python two-layer trace.",
            "NPU conversion and blending run on NPU; GPU copy/presentation remain.",
            "Every child compiles and qualifies all providers before readiness.",
            "Graphics reports include two settling seconds before the measured requests.",
            "Frame submission is not scanout; differing frame counts can confound throughput.",
            "Harness and renderer each report their own loaded runtime/driver versions.",
        ],
    }
    try:
        report["harness_loaded_stack"] = inspect_stack()
        if args.expected_npu_driver:
            require_npu_driver(report["harness_loaded_stack"], args.expected_npu_driver)
        report["windows_session"] = inspect_session()
        require_interactive_desktop(report["windows_session"])
        report["warmup"] = client.request()
        report["sustained_warmup"] = []
        warmup_begin = time.monotonic()
        while time.monotonic() - warmup_begin < args.warmup_seconds:
            report["sustained_warmup"].append(client.request())
            write_report(args.report, report)
        for round_index in range(args.rounds):
            for condition in trial_order(round_index, conditions):
                backend = condition_backend(condition)
                report["windows_session"] = inspect_session()
                require_interactive_desktop(report["windows_session"])
                with tempfile.TemporaryDirectory(prefix="npu-render-native-") as directory:
                    root = Path(directory)
                    ready, stop, graphics = (
                        root / name for name in ("ready", "stop", "graphics.json")
                    )
                    command = [
                        str(args.executable.resolve()),
                        "--backend",
                        backend,
                        "--width",
                        str(args.width),
                        "--height",
                        str(args.height),
                        "--fps",
                        str(args.fps),
                        "--seconds",
                        str(min(3600, args.timeout * args.requests + 60)),
                        "--ready-file",
                        str(ready),
                        "--stop-file",
                        str(stop),
                        "--report",
                        str(graphics),
                    ]
                    if condition == "NPU_CONV":
                        command += ["--npu-alpha", "convolution"]
                    if args.expected_npu_driver:
                        command += ["--expected-npu-driver", args.expected_npu_driver]
                    with (root / "child.log").open("w+", encoding="utf-8") as log:
                        process = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + 90
                            while not ready.exists():
                                if process.poll() is not None:
                                    log.seek(0)
                                    raise RuntimeError("renderer preparation failed: " + log.read())
                                if time.monotonic() >= deadline:
                                    raise TimeoutError("renderer readiness timed out")
                                time.sleep(0.05)
                            time.sleep(2)
                            power_before = power_status()
                            begin = time.time()
                            inference = [client.request() for _ in range(args.requests)]
                            end = time.time()
                            if process.poll() is not None:
                                raise RuntimeError("renderer ended before inference completed")
                            stop.write_text("stop\n", encoding="utf-8")
                            if process.wait(timeout=15):
                                log.seek(0)
                                raise RuntimeError("renderer failed: " + log.read())
                            captured = json.loads(graphics.read_text(encoding="utf-8"))
                            report["pending_trial"] = {
                                "round": round_index,
                                "condition": condition,
                                "backend": backend,
                                "session_before": report["windows_session"],
                                "session_after": inspect_session(),
                                "power_before": power_before,
                                "power_after": power_status(),
                                "inference_begin_unix_s": begin,
                                "inference_end_unix_s": end,
                                "inference": inference,
                                "graphics": captured,
                            }
                            require_interactive_desktop(report["pending_trial"]["session_after"])
                            if args.expected_npu_driver and (
                                captured.get("npu_driver_version") != args.expected_npu_driver
                            ):
                                raise RuntimeError(
                                    "renderer reported a different loaded NPU driver"
                                )
                            if not captured["passed"] or captured["session_id"] == 0:
                                raise RuntimeError(
                                    "renderer did not run successfully in an interactive session"
                                )
                            if captured["initial_present_hresult"] != 0 or any(
                                frame["present_hresult"] != 0 for frame in captured["frames"]
                            ):
                                raise RuntimeError(
                                    "a presentation was occluded or returned a nonzero status"
                                )
                            if (
                                condition == "NPU_CONV"
                                and captured.get("npu_alpha") != "convolution"
                            ):
                                raise RuntimeError("renderer did not use the requested NPU graph")
                            if (
                                condition == "NPU"
                                and captured.get("npu_alpha", "gather") != "gather"
                            ):
                                raise RuntimeError(
                                    "baseline NPU condition requires the gather graph"
                                )
                            if any(frame["backend"] != backend for frame in captured["frames"]):
                                raise RuntimeError("backend changed during a fixed-condition trial")
                            report["trials"].append(report["pending_trial"])
                            del report["pending_trial"]
                            report["comparisons"] = native_comparisons(report["trials"])
                            write_report(args.report, report)
                            print(
                                json.dumps(
                                    {"completed_round": round_index, "condition": condition}
                                ),
                                flush=True,
                            )
                        finally:
                            if process.poll() is None:
                                stop.write_text("stop\n", encoding="utf-8")
                                try:
                                    process.wait(timeout=15)
                                except subprocess.TimeoutExpired:
                                    process.terminate()
                                    process.wait(timeout=15)
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        write_report(args.report, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--inference-device", required=True)
    parser.add_argument("--expected-npu-driver", help="require the runtime-reported driver version")
    parser.add_argument("--prompt", default="Explain how a computer renders a translucent window.")
    parser.add_argument("--max-tokens", type=int, default=128)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--requests", type=int, default=3)
    parser.add_argument("--conditions", default="GPU,NPU,CPU,IDLE")
    parser.add_argument(
        "--warmup-seconds",
        type=float,
        default=0,
        help="additional sustained inference warmup before trials",
    )
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--report", type=Path, default=Path("results/native-contention.json"))
    args = parser.parse_args()
    report = run(args)
    print(json.dumps(report["comparisons"], indent=2))


if __name__ == "__main__":
    main()
