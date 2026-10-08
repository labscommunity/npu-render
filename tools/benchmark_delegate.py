"""Compare DWM with delegated composition of the same three cooperating windows."""

import argparse
import hashlib
import importlib.util
import json
import math
import os
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

from npu_render.benchmark import trial_order, write_report
from npu_render.inference import InferenceClient
from npu_render.windows_session import inspect_session, require_interactive_desktop


def validate_capture(captured, mode, fps, expected_driver, expected_graph="source-over"):
    if (
        not captured["passed"]
        or captured["validation_only"]
        or captured["cancelled"]
        or captured["recovered_to_dwm"]
        or captured["session_id"] == 0
        or captured["initial_mode"] != mode
        or captured["fps"] != fps
        or captured["npu_graph"] != expected_graph
    ):
        raise RuntimeError("delegation did not complete in the requested interactive mode")
    if (
        captured["opaque_oracle_max_error"] != 0
        or captured["end_screen_max_error"] > 1
        or captured["pixel_qualification_cases"] != 8
        or captured["source_count"] != 3
        or captured["restored_source_windows"] != 3
        or captured["whole_desktop_offloaded"]
    ):
        raise RuntimeError("pixel correctness, scope or source restoration gate failed")
    if captured["npu_execution_device"] != "NPU" or (
        expected_driver and captured["npu_driver_version"] != expected_driver
    ):
        raise RuntimeError("NPU execution or loaded driver mismatch")
    checks = captured["screen_checks"]
    if (
        len(checks) != 3
        or {c["mode"] for c in checks} != {"DWM", "NPU", "GPU"}
        or any(c["maximum_rgb_error"] > 1 for c in checks)
    ):
        raise RuntimeError("on-screen source arrangement did not pass the pixel oracle")
    frames = captured["frames"]
    wall = captured["live_wall_seconds"]
    if not frames or not math.isfinite(wall) or wall <= 0 or len(frames) / wall < fps * 0.99:
        raise RuntimeError("delegated trace did not sustain 99% of the requested cadence")
    for frame in frames:
        if (
            frame["mode"] != mode
            or frame["visible_source_windows"] != (3 if mode == "DWM" else 0)
            or frame["npu_blends"] != (3 if mode == "NPU" else 0)
            or frame["gpu_blends"] != (3 if mode == "GPU" else 0)
            or frame["present_hresult"] != (None if mode == "DWM" else 0)
        ):
            raise RuntimeError("source retirement, execution or presentation gate failed")
    if captured["move_calls"] != (3 * len(frames) if mode == "DWM" else 0):
        raise RuntimeError("unexpected original-window movement during delegation")


def comparisons(trials):
    result = {}
    for numerator, denominator in (
        ("NPU", "DWM"),
        ("GPU", "DWM"),
        ("NPU", "GPU"),
        ("NPU_SELECT", "DWM"),
        ("NPU_SELECT", "NPU"),
        ("NPU_SELECT", "GPU"),
    ):
        pairs = []
        for index in sorted({t["round"] for t in trials}):
            group = {t["condition"]: t for t in trials if t["round"] == index}
            if not {numerator, denominator}.issubset(group):
                continue
            rates = {
                mode: statistics.median(
                    r["tokens_per_second_end_to_end"] for r in group[mode]["inference"]
                )
                for mode in (numerator, denominator)
            }
            if not all(math.isfinite(rate) and rate > 0 for rate in rates.values()):
                raise RuntimeError("positive finite inference throughput required")
            pairs.append({"round": index, "ratio": rates[numerator] / rates[denominator]})
        result[f"{numerator.lower()}_over_{denominator.lower()}"] = {
            "paired_ratios": pairs,
            "median_ratio": statistics.median(p["ratio"] for p in pairs) if pairs else None,
        }
    return result


def run(args):
    if os.name != "nt" or not args.executable.is_file():
        raise ValueError("Windows and a built nr_desktop_delegate executable are required")
    if (
        args.rounds < 1
        or args.requests < 1
        or not math.isfinite(args.warmup_seconds)
        or not 0 <= args.warmup_seconds <= 3600
    ):
        raise ValueError("positive rounds/requests and a warmup of 0..3600 seconds are required")
    modes = args.conditions.split(",")
    if (
        not modes
        or len(set(modes)) != len(modes)
        or set(modes) - {"DWM", "NPU", "GPU", "NPU_SELECT"}
    ):
        raise ValueError("conditions must be a unique list of DWM,NPU,GPU,NPU_SELECT")
    package = importlib.util.find_spec("openvino")
    environment = os.environ.copy()
    environment["PATH"] = (
        str(Path(package.origin).parent / "libs") + os.pathsep + environment["PATH"]
    )
    client = InferenceClient(args.endpoint, args.model, args.prompt, args.max_tokens, args.timeout)
    report = {
        "schema_version": 1,
        "status": "running",
        "scope": "three cooperating Windows surfaces; outer Windows desktop remains on DWM",
        "model": args.model,
        "inference_device_declared": args.inference_device,
        "inference_device_verified_by_harness": False,
        "executable_sha256": hashlib.sha256(args.executable.read_bytes()).hexdigest(),
        "prompt_sha256": hashlib.sha256(args.prompt.encode()).hexdigest(),
        "conditions": modes,
        "fps": args.fps,
        "max_tokens": args.max_tokens,
        "requests_per_trial": args.requests,
        "warmup_seconds_requested": args.warmup_seconds,
        "trials": [],
        "notes": [
            "DWM moves three real opaque GDI windows over a static canvas.",
            "Delegated modes hide the owned windows and compose their surfaces into the canvas.",
            "All modes pass the same offscreen and on-screen pixel checks before measurement.",
            "CPU placement, GPU transfers and the outer DWM remain in the delegated path.",
            "99% loop cadence is a minimum gate; skips, deadlines and display data need review.",
            "An accepted trial does not establish a system-wide desktop switch or a Tahoma gain.",
        ],
    }
    try:
        require_interactive_desktop(inspect_session())
        report["warmup"] = client.request()
        report["sustained_warmup"] = []
        started = time.monotonic()
        while time.monotonic() - started < args.warmup_seconds:
            report["sustained_warmup"].append(client.request())
            write_report(args.report, report)
        for index in range(args.rounds):
            for condition in trial_order(index, modes):
                mode = "NPU" if condition == "NPU_SELECT" else condition
                graph = "opaque-select" if condition == "NPU_SELECT" else "source-over"
                before = inspect_session()
                require_interactive_desktop(before)
                with tempfile.TemporaryDirectory(prefix="npu-render-delegate-") as directory:
                    root = Path(directory)
                    ready, stop, output = (
                        root / name for name in ("ready", "stop", "graphics.json")
                    )
                    command = [
                        str(args.executable.resolve()),
                        "--mode",
                        mode,
                        "--width",
                        str(args.width),
                        "--height",
                        str(args.height),
                        "--fps",
                        str(args.fps),
                        "--seconds",
                        str(min(1800, args.requests * args.timeout + 60)),
                        "--ready-file",
                        str(ready),
                        "--stop-file",
                        str(stop),
                        "--report",
                        str(output),
                    ]
                    if args.expected_npu_driver:
                        command += ["--expected-npu-driver", args.expected_npu_driver]
                    command += ["--npu-graph", graph]
                    with (root / "child.log").open("w+", encoding="utf-8") as log:
                        process = subprocess.Popen(command, env=environment, stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + 120
                            while not ready.exists():
                                if process.poll() is not None or time.monotonic() >= deadline:
                                    log.seek(0)
                                    raise RuntimeError(
                                        "delegation preparation failed: " + log.read()
                                    )
                                time.sleep(0.05)
                            time.sleep(2)
                            begin = time.time()
                            inference = [client.request() for _ in range(args.requests)]
                            end = time.time()
                            if process.poll() is not None:
                                raise RuntimeError("delegation ended during inference")
                            stop.write_text("stop\n", encoding="utf-8")
                            exit_code = process.wait(timeout=20)
                            captured = json.loads(output.read_text(encoding="utf-8"))
                            report["pending_trial"] = {
                                "round": index,
                                "condition": condition,
                                "session_before": before,
                                "session_after": inspect_session(),
                                "inference_begin_unix_s": begin,
                                "inference_end_unix_s": end,
                                "inference": inference,
                                "graphics": captured,
                            }
                            require_interactive_desktop(report["pending_trial"]["session_after"])
                            validate_capture(
                                captured, mode, args.fps, args.expected_npu_driver, graph
                            )
                            if exit_code:
                                raise RuntimeError("delegation process failed")
                            report["trials"].append(report.pop("pending_trial"))
                            report["comparisons"] = comparisons(report["trials"])
                            write_report(args.report, report)
                            print(
                                json.dumps({"completed_round": index, "condition": condition}),
                                flush=True,
                            )
                        finally:
                            if process.poll() is None:
                                stop.write_text("stop\n", encoding="utf-8")
                                try:
                                    process.wait(timeout=20)
                                except subprocess.TimeoutExpired:
                                    process.terminate()
                                    process.wait(timeout=10)
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        write_report(args.report, report)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--inference-device", required=True)
    parser.add_argument("--expected-npu-driver")
    parser.add_argument("--conditions", default="DWM,NPU,GPU")
    parser.add_argument(
        "--prompt", default="Explain how a computer composes overlapping desktop windows."
    )
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--requests", type=int, default=3)
    parser.add_argument("--max-tokens", type=int, default=128)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--warmup-seconds", type=float, default=120)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--report", required=True, type=Path)
    result = run(parser.parse_args())
    print(json.dumps(result.get("comparisons", {}), indent=2))


if __name__ == "__main__":
    main()
