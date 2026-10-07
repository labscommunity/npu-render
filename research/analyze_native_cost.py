"""Join native contention trials to read-only IGCL telemetry without guessing missing metrics."""

import argparse
import json
import math
import statistics
from collections import Counter
from pathlib import Path


def stats(values):
    return {
        "count": len(values),
        "mean": statistics.fmean(values) if values else None,
        "median": statistics.median(values) if values else None,
        "minimum": min(values) if values else None,
        "maximum": max(values) if values else None,
    }


def metric(sample, name, units):
    if sample.get("v2_status") != 0 and sample.get("v1_status") != 0:
        return None
    item = sample.get(name, {})
    value = item.get("value")
    if (
        item.get("supported") is not True
        or item.get("units") != units
        or isinstance(value, bool)
        or not isinstance(value, (int, float))
        or not math.isfinite(value)
    ):
        return None
    return value


def counter_rate(left, right, name, units):
    """Require one adapter, valid units, and increasing clocks/counters."""
    if left["index"] != right["index"] or left["unix_s"] >= right["unix_s"]:
        return None
    values = [metric(s, n, u) for s in (left, right) for n, u in (("timeStamp", 7), (name, units))]
    if any(value is None for value in values):
        return None
    t0, v0, t1, v1 = values
    if t1 <= t0 or v1 < v0:
        return None
    return (v1 - v0) / (t1 - t0)


def telemetry_interval(samples, begin, end, index):
    # Filter by phase AFTER forming adjacent pairs, so invalid rows never bridge gaps.
    rows = [s for s in samples if s["index"] == index]
    if any(right["unix_s"] <= left["unix_s"] for left, right in zip(rows, rows[1:], strict=False)):
        raise ValueError("telemetry must be ordered with unique sample times per adapter")
    selected = [s for s in rows if begin <= s["unix_s"] <= end]
    pairs = [
        (left, right)
        for left, right in zip(rows, rows[1:], strict=False)
        if begin <= left["unix_s"] < right["unix_s"] <= end
    ]
    rates = {}
    for field, units, scale, label in (
        ("gpuEnergyCounter", 6, 1, "gpu_energy_delta_watts"),
        ("globalActivityCounter", 7, 100, "global_activity_percent"),
        ("renderComputeActivityCounter", 7, 100, "render_compute_activity_percent"),
    ):
        values = [counter_rate(left, right, field, units) for left, right in pairs]
        rates[label] = stats([v * scale for v in values if v is not None])
    clocks = [metric(s, "gpuCurrentClockFrequency", 0) for s in selected]
    return {
        "samples": len(selected),
        "adjacent_intervals": len(pairs),
        "gpu_clock_mhz": stats([v for v in clocks if v is not None]),
        **rates,
        "reported_limit_true_samples": {
            key: sum(s.get(key) is True for s in selected)
            for key in ("gpuPowerLimited", "gpuTemperatureLimited", "gpuCurrentLimited")
        },
    }


def summarize(benchmark, telemetry, index=0):
    adapters = [r for r in telemetry if r.get("kind") == "adapter" and r.get("index") == index]
    if len(adapters) != 1 or adapters[0].get("status") != 0:
        raise ValueError("a single successfully identified telemetry adapter is required")
    if benchmark["status"] != "complete":
        raise ValueError("benchmark did not complete")
    samples = [r for r in telemetry if r.get("kind") == "sample"]
    trials = []
    for trial in benchmark["trials"]:
        graphics = trial["graphics"]
        begin, end = trial["inference_begin_unix_s"], trial["inference_end_unix_s"]
        frames = [
            f
            for f in graphics["frames"]
            if begin <= graphics["started_unix_s"] + f["elapsed_s"]
            and graphics["started_unix_s"] + f["elapsed_s"] + f["work_ms"] / 1000 <= end
        ]
        wall = graphics["live_wall_seconds"]
        presentations = sum(f.get("presented", True) for f in graphics["frames"])
        trials.append(
            {
                "round": trial["round"],
                "condition": trial["condition"],
                "npu_alpha": graphics["npu_alpha"],
                "power_before": trial.get("power_before"),
                "power_after": trial.get("power_after"),
                "throughput_tokens_per_second": stats(
                    [r["tokens_per_second_end_to_end"] for r in trial["inference"]]
                ),
                "frame_phases_ms": {
                    phase: stats([f[phase] for f in frames])
                    for phase in ("scene_ms", "upload_ms", "compose_ms", "present_ms", "work_ms")
                },
                "frames_wholly_in_inference": len(frames),
                "whole_live_frames": len(graphics["frames"]),
                "whole_live_submissions_per_second": presentations / wall,
                "whole_live_cpu_core_equivalents": graphics["live_process_cpu_seconds"] / wall,
                "skipped_slots": graphics["skipped_slots"],
                "deadline_misses": graphics["deadline_misses"],
                "nonzero_present_statuses": sum(
                    f.get("presented", True) and f["present_hresult"] != 0
                    for f in graphics["frames"]
                ),
                "telemetry": telemetry_interval(samples, begin, end, index),
            }
        )
        if "diagnostic" in graphics:
            trials[-1].update(
                diagnostic=graphics["diagnostic"],
                diagnostic_wait_ms=graphics["diagnostic_wait_ms"],
                full_graphics_trace=graphics["full_graphics_trace"],
                whole_live_iterations=len(graphics["frames"]),
                whole_live_iterations_per_second=len(graphics["frames"]) / wall,
                whole_live_presentations=presentations,
                whole_live_compositions=sum(f["composed"] for f in graphics["frames"]),
            )
            trials[-1]["frame_phases_ms"]["wait_ms"] = stats([f["wait_ms"] for f in frames])
    conditions = {}
    for condition in sorted({t["condition"] for t in trials}):
        group = [t for t in trials if t["condition"] == condition]
        conditions[condition] = {
            "trial_median_throughput": stats(
                [t["throughput_tokens_per_second"]["median"] for t in group]
            ),
            "trial_median_compose_ms": stats(
                [
                    t["frame_phases_ms"]["compose_ms"]["median"]
                    for t in group
                    if t["frame_phases_ms"]["compose_ms"]["median"] is not None
                ]
            ),
            "trial_live_cpu_core_equivalents": stats(
                [t["whole_live_cpu_core_equivalents"] for t in group]
            ),
            **{
                "trial_mean_" + key: stats(
                    [
                        t["telemetry"][key]["mean"]
                        for t in group
                        if t["telemetry"][key]["mean"] is not None
                    ]
                )
                for key in (
                    "gpu_clock_mhz",
                    "gpu_energy_delta_watts",
                    "global_activity_percent",
                    "render_compute_activity_percent",
                )
            },
        }
    requests = [r for t in benchmark["trials"] for r in t["inference"]]
    return {
        "scope": "application blend graph and concurrent Tahoma; not replacement of DWM",
        "adapter": adapters[0],
        "comparisons": benchmark["comparisons"],
        "conditions": conditions,
        "trials": trials,
        "completion_token_counts": dict(Counter(r["completion_tokens"] for r in requests)),
        "output_hashes": dict(Counter(r["output_sha256"] for r in requests)),
        "telemetry_statuses": dict(
            Counter(str((s.get("v2_status"), s.get("v1_status"))) for s in samples)
        ),
        "notes": [
            "Clock/energy/activity are adapter-wide; they cannot attribute costs to one process.",
            "GPU energy is not CPU package or NPU energy; unsupported metrics are not zeros.",
            "Energy/activity use adjacent samples wholly inside each inference window.",
            "Device timeStamp is used for deltas only; phase matching uses captured host unix_s.",
            "CPU core equivalents include settling and exclude compilation/qualification.",
            "A false limit flag does not establish absence of package power or thermal limits.",
            "Paired ratios compare medians within each round; condition summaries are descriptive.",
            "Graphics times are host-waited wall times, not isolated GPU execution durations.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("benchmark", type=Path)
    parser.add_argument("telemetry", type=Path)
    parser.add_argument("--adapter-index", type=int, default=0)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    result = summarize(
        json.loads(args.benchmark.read_text(encoding="utf-8-sig")),
        [json.loads(line) for line in args.telemetry.read_text(encoding="utf-8-sig").splitlines()],
        args.adapter_index,
    )
    args.output.write_text(json.dumps(result, indent=2, allow_nan=False) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
