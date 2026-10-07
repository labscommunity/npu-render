"""Summarize the October desktop experiment without inferring NPU savings."""

import argparse
import csv
import io
import json
import statistics
from collections import Counter
from datetime import datetime
from pathlib import Path


def read_text(path):
    data = path.read_bytes()
    return data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")


def utc(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def stats(values):
    return {
        "count": len(values),
        "mean": statistics.fmean(values) if values else None,
        "median": statistics.median(values) if values else None,
        "maximum": max(values) if values else None,
    }


def summarize(root):
    benchmark = json.loads(read_text(root / "desktop-benchmark.json"))
    adapters = json.loads(read_text(root / "adapters.json"))["adapters"]
    hardware = [a["instance_luid"].lower() for a in adapters if a.get("hardware") is True]
    counters = [
        json.loads(line) for line in read_text(root / "desktop-counters.jsonl").splitlines()
    ]
    metadata = json.loads(read_text(root / "desktop-counters.jsonl.metadata.json"))
    if metadata["fatal_error"] or metadata["samples"] != len(counters):
        raise ValueError("capture was incomplete")
    trials = []
    for trial in benchmark["trials"]:
        selected = [
            s
            for s in counters
            if utc(s["query_started_utc"]) >= trial["inference_begin_unix_s"]
            and utc(s["at_utc"]) <= trial["inference_end_unix_s"]
        ]
        dwm, inference = [], []
        for sample in selected:
            names = {p["pid"]: p["name"] for p in sample["processes"]}
            for engine in sample["engines"]:
                if engine["counter_status"] not in (0, 1):
                    continue
                if not any(
                    engine["adapter_engine"].lower().startswith(luid + "_") for luid in hardware
                ):
                    continue
                value = engine["utilization_percent"]
                if value is None or not 0 <= value <= 100:
                    continue
                if names.get(engine["pid"]) == "dwm" and engine["engine_type"].lower() == "3d":
                    dwm.append(value)
                if (
                    names.get(engine["pid"]) == "cascadia-ov"
                    and engine["engine_type"].lower() == "neural"
                ):
                    inference.append(value)
        trials.append(
            {
                "round": trial["round"],
                "condition": trial["condition"],
                "complete_query_intervals": len(selected),
                "dwm_hardware_3d_sample_percent": stats(dwm),
                "tahoma_hardware_neural_sample_percent": stats(inference),
                "throughput_tokens_per_second": stats(
                    [r["tokens_per_second_end_to_end"] for r in trial["inference"]]
                ),
            }
        )
    conditions = {}
    for condition in ("static", "move"):
        requests = [
            r for t in benchmark["trials"] if t["condition"] == condition for r in t["inference"]
        ]
        conditions[condition] = {
            "pooled_throughput_tokens_per_second": stats(
                [r["tokens_per_second_end_to_end"] for r in requests]
            ),
            "time_to_first_content_ms": stats([r["time_to_first_content_ms"] for r in requests]),
            "trial_mean_dwm_3d_percent": stats(
                [
                    t["dwm_hardware_3d_sample_percent"]["mean"]
                    for t in trials
                    if t["condition"] == condition
                ]
            ),
        }
    presentation = list(csv.DictReader(io.StringIO(read_text(root / "desktop-presentmon.csv"))))
    return {
        "schema_version": 1,
        "scope": "controlled Windows desktop-motion sensitivity, not NPU offload or predicted gain",
        "benchmark_status": benchmark["status"],
        "paired_ratios": benchmark["paired_ratios"],
        "median_move_over_static": benchmark["median_move_over_static"],
        "conditions": conditions,
        "trials": trials,
        "completion_token_counts": dict(
            Counter(r["completion_tokens"] for t in benchmark["trials"] for r in t["inference"])
        ),
        "output_hashes": dict(
            Counter(r["output_sha256"] for t in benchmark["trials"] for r in t["inference"])
        ),
        "presentmon": {
            "version": "2.6.0",
            "rows": len(presentation),
            "process_counts": dict(Counter(r["Application"] for r in presentation)),
            "mode_counts": dict(Counter(r["PresentMode"] for r in presentation)),
            "numeric_display_latency_rows": sum(
                r["MsUntilDisplayed"] != "NA" for r in presentation
            ),
            "phase_alignment": "not performed: date_time output has a timezone conversion anomaly",
        },
        "counter_capture": metadata,
        "notes": [
            "Only independently primed queries wholly inside each inference interval are included.",
            "Engine samples describe observed intervals, not continuous utilization.",
            "This capture has one hardware 3D engine for DWM; engines are not added together.",
            "Neural here is an Arc GPU engine label, established from its adapter LUID, not NPU.",
            "Paired median ratios and pooled throughput medians answer different questions.",
            "Window movement also changes CPU and memory work; no causal decomposition is claimed.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_directory", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    args.output.write_text(
        json.dumps(summarize(args.capture_directory), indent=2, allow_nan=False) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
