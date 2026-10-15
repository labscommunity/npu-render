"""Counterbalanced real-inference trials against identical paced graphics traces."""

import argparse
import hashlib
import json
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from .contracts import Composition
from .inference import InferenceClient
from .live import distribution, run_session
from .presentation import NullPresenter, WindowPresenter
from .renderer import Renderer


def trial_order(round_index: int, conditions: list[str]) -> list[str]:
    """Rotate conditions and reverse every full cycle to reduce order bias."""
    offset = round_index % len(conditions)
    order = conditions[offset:] + conditions[:offset]
    return order if (round_index // len(conditions)) % 2 == 0 else list(reversed(order))


def comparisons(trials: list[dict]) -> dict:
    ratios = []
    for round_index in sorted({trial["round"] for trial in trials}):
        group = {trial["condition"]: trial for trial in trials if trial["round"] == round_index}
        if not {"GPU", "NPU"}.issubset(group):
            continue
        rates = {}
        for condition in ("GPU", "NPU"):
            rows = group[condition]["inference"]
            values = [row["tokens_per_second_end_to_end"] for row in rows]
            if values and all(value is not None for value in values):
                rates[condition] = distribution(values)["median"]
        if len(rates) == 2:
            ratios.append({"round": round_index, "npu_over_gpu": rates["NPU"] / rates["GPU"]})
    return {
        "paired_end_to_end_throughput_ratios": ratios,
        "ratio_distribution": distribution([row["npu_over_gpu"] for row in ratios]),
        "scope": "measured application composition conditions; not whole-desktop offload",
    }


def write_report(path: Path, report: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    temporary.replace(path)


def benchmark(args) -> dict:
    conditions = [item.strip().upper() for item in args.conditions.split(",")]
    if not conditions or len(set(conditions)) != len(conditions):
        raise ValueError("conditions must be a nonempty unique list")
    if set(conditions) - {"GPU", "NPU", "CPU", "IDLE"}:
        raise ValueError("conditions must be GPU,NPU,CPU,IDLE")
    if args.rounds < 1 or args.requests < 1 or args.settle_seconds < 0:
        raise ValueError("rounds/requests must be positive and settle_seconds nonnegative")
    client = InferenceClient(args.endpoint, args.model, args.prompt, args.max_tokens, args.timeout)
    spec = Composition(args.width, args.height, args.layers)
    renderer = Renderer(spec, "CPU")
    presenter = NullPresenter() if args.headless else WindowPresenter(spec.width, spec.height)
    report = {
        "schema_version": 1,
        "status": "running",
        "model": args.model,
        "prompt_sha256": hashlib.sha256(args.prompt.encode()).hexdigest(),
        "max_tokens": args.max_tokens,
        "temperature": 0,
        "inference_device_declared": args.inference_device,
        "inference_device_verified_by_harness": False,
        "requests_per_trial": args.requests,
        "settle_seconds": args.settle_seconds,
        "trials": [],
        "notes": [
            "Keep server, model, cache policy, power mode and other load fixed.",
            "Client content/chunk times are not engine token-generation timestamps.",
            "Display submission does not establish physical presentation timing.",
            "Review skipped frames: different delivered load can confound comparisons.",
        ],
    }
    try:
        report["warmup"] = client.request()
        write_report(args.report, report)
        with ThreadPoolExecutor(max_workers=1, thread_name_prefix="npu-render-inference") as worker:
            for round_index in range(args.rounds):
                for condition in trial_order(round_index, conditions):
                    renderer.switch("CPU" if condition == "IDLE" else condition)
                    if args.settle_seconds:
                        run_session(
                            renderer,
                            presenter,
                            seconds=args.settle_seconds,
                            fps=args.fps,
                            idle=condition == "IDLE",
                        )
                    stop = threading.Event()
                    ready = threading.Event()

                    def infer_batch(ready=ready, stop=stop):
                        if not ready.wait(timeout=30):
                            raise RuntimeError("renderer did not become ready")
                        requests = []
                        try:
                            for _ in range(args.requests):
                                requests.append(client.request())
                            return requests
                        finally:
                            stop.set()

                    future = worker.submit(infer_batch)
                    begin = time.perf_counter()
                    graphics = run_session(
                        renderer,
                        presenter,
                        seconds=args.timeout * args.requests + 30,
                        fps=args.fps,
                        stop=stop,
                        on_ready=ready.set,
                        idle=condition == "IDLE",
                    )
                    if graphics["stop_reason"] != "stop_event":
                        raise RuntimeError("graphics replay ended before inference completed")
                    inference = future.result()
                    report["trials"].append(
                        {
                            "round": round_index,
                            "condition": condition,
                            "wall_s": time.perf_counter() - begin,
                            "graphics": graphics,
                            "inference": inference,
                        }
                    )
                    report["comparisons"] = comparisons(report["trials"])
                    write_report(args.report, report)
        report["status"] = "complete"
    except Exception as error:
        report.update(status="failed", error=str(error))
        raise
    finally:
        presenter.close()
        write_report(args.report, report)
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--endpoint", required=True, help="Tahoma API base including /v1")
    parser.add_argument("--model", required=True)
    parser.add_argument("--inference-device", required=True, help="declared; verify in server logs")
    parser.add_argument("--prompt", default="Explain how a computer renders a translucent window.")
    parser.add_argument("--max-tokens", type=int, default=128)
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--requests", type=int, default=3)
    parser.add_argument("--conditions", default="GPU,NPU,CPU,IDLE")
    parser.add_argument("--settle-seconds", type=float, default=2)
    parser.add_argument("--width", type=int, default=1280)
    parser.add_argument("--height", type=int, default=720)
    parser.add_argument("--layers", type=int, default=2)
    parser.add_argument("--fps", type=float, default=60)
    parser.add_argument("--headless", action="store_true")
    parser.add_argument("--report", type=Path, default=Path("results/contention.json"))
    args = parser.parse_args(argv)
    try:
        report = benchmark(args)
    except (ValueError, RuntimeError, OSError) as error:
        print(json.dumps({"error": str(error), "report": str(args.report)}))
        return 1
    print(json.dumps({"report": str(args.report), "comparisons": report["comparisons"]}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
