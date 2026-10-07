"""Measure Tahoma sensitivity to Windows DWM activity; does not offload DWM."""

import argparse
import hashlib
import json
import os
import statistics
import subprocess
import tempfile
import time
from pathlib import Path

from npu_render.benchmark import trial_order, write_report
from npu_render.inference import InferenceClient
from npu_render.windows_session import inspect_session, require_interactive_desktop


def run(args):
    if os.name != "nt" or not args.executable.is_file():
        raise ValueError("requires Windows and a built nr_desktop_motion executable")
    if args.rounds < 1 or args.requests < 1 or args.requests * args.timeout + 30 > 300:
        raise ValueError("positive rounds/requests and a per-trial timeout below 270s required")
    client = InferenceClient(args.endpoint, args.model, args.prompt, args.max_tokens, args.timeout)
    report = {
        "schema_version": 1,
        "status": "running",
        "scope": "Tahoma sensitivity to controlled Windows DWM window motion; no NPU offload",
        "model": args.model,
        "inference_device_declared": args.inference_device,
        "inference_device_verified_by_harness": False,
        "executable_sha256": hashlib.sha256(args.executable.read_bytes()).hexdigest(),
        "prompt_sha256": hashlib.sha256(args.prompt.encode()).hexdigest(),
        "max_tokens": args.max_tokens,
        "requests_per_trial": args.requests,
        "trials": [],
        "notes": [
            "Both conditions create the same three windows; only their movement changes.",
            "Motion changes CPU/window management, GPU composition and memory activity together.",
            "Any difference is not a prediction of the gain from an NPU compositor.",
            "Movement calls do not prove displayed frames; inspect presentation telemetry.",
            "Controlled desktop activity, not a recording of the user's usual workload.",
        ],
    }
    try:
        require_interactive_desktop(inspect_session())
        report["warmup"] = client.request()
        for round_index in range(args.rounds):
            for condition in trial_order(round_index, ["static", "move"]):
                session = inspect_session()
                require_interactive_desktop(session)
                with tempfile.TemporaryDirectory(prefix="npu-render-desktop-") as directory:
                    root = Path(directory)
                    ready, stop, graphics = (
                        root / name for name in ("ready", "stop", "graphics.json")
                    )
                    command = [
                        str(args.executable.resolve()),
                        "--mode",
                        condition,
                        "--seconds",
                        str(int(args.timeout * args.requests + 30)),
                        "--ready-file",
                        str(ready),
                        "--stop-file",
                        str(stop),
                        "--report",
                        str(graphics),
                    ]
                    with (root / "child.log").open("w+", encoding="utf-8") as log:
                        process = subprocess.Popen(command, stdout=log, stderr=log)
                        try:
                            deadline = time.monotonic() + 15
                            while not ready.exists():
                                if process.poll() is not None or time.monotonic() >= deadline:
                                    log.seek(0)
                                    raise RuntimeError(
                                        "desktop workload readiness failed: " + log.read()
                                    )
                                time.sleep(0.05)
                            time.sleep(2)
                            begin = time.time()
                            requests = [client.request() for _ in range(args.requests)]
                            end = time.time()
                            if process.poll() is not None:
                                raise RuntimeError("desktop workload ended during inference")
                            stop.write_text("stop\n", encoding="utf-8")
                            if process.wait(timeout=10):
                                log.seek(0)
                                raise RuntimeError("desktop workload failed: " + log.read())
                            captured = json.loads(graphics.read_text(encoding="utf-8"))
                            after = inspect_session()
                            report["pending_trial"] = {
                                "round": round_index,
                                "condition": condition,
                                "session_before": session,
                                "session_after": after,
                                "inference_begin_unix_s": begin,
                                "inference_end_unix_s": end,
                                "inference": requests,
                                "graphics": captured,
                            }
                            require_interactive_desktop(after)
                            if captured["cancelled"] or captured["mode"] != condition:
                                raise RuntimeError(
                                    "workload was cancelled or used the wrong condition"
                                )
                            if captured["ticks"] / captured["elapsed_seconds"] < 55:
                                raise RuntimeError(
                                    "window-motion loop could not sustain 55 ticks/s"
                                )
                            report["trials"].append(report.pop("pending_trial"))
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
                                    process.wait(timeout=5)
                                except subprocess.TimeoutExpired:
                                    process.kill()
                                    process.wait(timeout=5)
        ratios = []
        for round_index in range(args.rounds):
            rates = {
                t["condition"]: statistics.median(
                    r["tokens_per_second_end_to_end"] for r in t["inference"]
                )
                for t in report["trials"]
                if t["round"] == round_index
            }
            ratios.append(
                {"round": round_index, "move_over_static": rates["move"] / rates["static"]}
            )
        report["paired_ratios"] = ratios
        report["median_move_over_static"] = statistics.median(r["move_over_static"] for r in ratios)
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
    write_report(args.report, report)
    return 0 if report["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--endpoint", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--inference-device", required=True)
    parser.add_argument(
        "--prompt", default="Explain how a computer composes overlapping desktop windows."
    )
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--requests", type=int, default=3)
    parser.add_argument("--max-tokens", type=int, default=128)
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--report", required=True, type=Path)
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()
