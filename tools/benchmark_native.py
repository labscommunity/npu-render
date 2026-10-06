"""Counterbalance native D3D12/NPU/CPU windows against a real inference server."""

import argparse
import hashlib
import importlib.util
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from npu_render.benchmark import comparisons, trial_order, write_report
from npu_render.inference import InferenceClient


def run(args):
    if os.name != "nt":
        raise RuntimeError("the native window requires Windows and Intel NPU hardware")
    if args.rounds < 1 or args.requests < 1 or not args.executable.is_file():
        raise ValueError("positive rounds/requests and a built native executable are required")
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
        "settle_seconds": 2,
        "trials": [],
        "notes": [
            "One BGRA8 foreground surface; differs from the Python two-layer trace.",
            "NPU conversion and blending run on NPU; GPU copy/presentation remain.",
            "Every child compiles and qualifies all providers before readiness.",
            "Graphics reports include two settling seconds before the measured requests.",
            "Frame submission is not scanout; differing frame counts can confound throughput.",
        ],
    }
    try:
        report["warmup"] = client.request()
        for round_index in range(args.rounds):
            for condition in trial_order(round_index, ["GPU", "NPU", "CPU", "IDLE"]):
                with tempfile.TemporaryDirectory(prefix="npu-render-native-") as directory:
                    root = Path(directory)
                    ready, stop, graphics = (
                        root / name for name in ("ready", "stop", "graphics.json")
                    )
                    command = [
                        str(args.executable.resolve()),
                        "--backend",
                        condition,
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
                                "inference_begin_unix_s": begin,
                                "inference_end_unix_s": end,
                                "inference": inference,
                                "graphics": captured,
                            }
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
                            if any(frame["backend"] != condition for frame in captured["frames"]):
                                raise RuntimeError("backend changed during a fixed-condition trial")
                            report["trials"].append(
                                {
                                    "round": round_index,
                                    "condition": condition,
                                    "inference_begin_unix_s": begin,
                                    "inference_end_unix_s": end,
                                    "inference": inference,
                                    "graphics": captured,
                                }
                            )
                            del report["pending_trial"]
                            report["comparisons"] = comparisons(report["trials"])
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
    parser.add_argument("--prompt", default="Explain how a computer renders a translucent window.")
    parser.add_argument("--max-tokens", type=int, default=128)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--requests", type=int, default=3)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--fps", type=int, default=60)
    parser.add_argument("--report", type=Path, default=Path("results/native-contention.json"))
    args = parser.parse_args()
    report = run(args)
    print(json.dumps(report["comparisons"], indent=2))


if __name__ == "__main__":
    main()
