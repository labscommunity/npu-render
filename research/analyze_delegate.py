"""Join cooperating-window trials to process/adapter and IGCL measurements."""

import argparse
import json
import math
from collections import Counter, defaultdict
from pathlib import Path

from analyze_desktop_capture import read_text, utc
from analyze_native_cost import stats, telemetry_interval


def summarize(root, benchmark_name):
    benchmark = json.loads(read_text(root / benchmark_name))
    if benchmark["status"] != "complete":
        raise ValueError("a completed benchmark is required")
    route = json.loads(read_text(root / "windows-route.json"))
    gpu = next(d for d in route["devices"] if d["openvino_device"] == "GPU")
    if gpu["dxgi_lookup_hresult"] != 0 or gpu["d3d11_create_hresult"] != 0:
        raise ValueError("a verified GPU adapter identity is required")
    luid = gpu["instance_luid"].lower()
    server = json.loads(read_text(root / "delegate-server.json"))
    counters = [json.loads(s) for s in read_text(root / "delegate-gpu.jsonl").splitlines()]
    metadata = json.loads(read_text(root / "delegate-gpu.jsonl.metadata.json"))
    if metadata["fatal_error"] or metadata["samples"] != len(counters):
        raise ValueError("counter capture incomplete")
    telemetry = [json.loads(s) for s in read_text(root / "delegate-igcl.jsonl").splitlines()]
    adapters = [r for r in telemetry if r.get("kind") == "adapter" and r.get("status") == 0]
    if len(adapters) != 1:
        raise ValueError("one identified IGCL adapter required")
    adapter = adapters[0]
    if f"luid_0x{adapter['luid_high']:08x}_0x{adapter['luid_low']:08x}" != luid:
        raise ValueError("IGCL and Windows adapter identities differ")
    samples = [s for s in telemetry if s.get("kind") == "sample"]
    trials = []
    for trial in benchmark["trials"]:
        begin, end = trial["inference_begin_unix_s"], trial["inference_end_unix_s"]
        graphics = trial["graphics"]
        selected = [
            s for s in counters if utc(s["query_started_utc"]) >= begin and utc(s["at_utc"]) <= end
        ]
        engines = defaultdict(list)
        rejected = 0
        for sample in selected:
            processes = {p["pid"]: p for p in sample["processes"]}
            for engine in sample["engines"]:
                if not engine["adapter_engine"].lower().startswith(luid + "_"):
                    continue
                process = processes.get(engine["pid"], {})
                if engine["pid"] == server["pid"]:
                    actor = "tahoma"
                elif engine["pid"] == graphics["pid"]:
                    actor = "delegate"
                elif (
                    process.get("name") == "dwm"
                    and process.get("session_id") == graphics["session_id"]
                ):
                    actor = "dwm"
                else:
                    continue
                value = engine["utilization_percent"]
                if (
                    engine["counter_status"] not in (0, 1)
                    or value is None
                    or not math.isfinite(value)
                    or not 0 <= value <= 100
                ):
                    rejected += 1
                    continue
                # Retain individual engines. Do not add utilization across engines.
                key = actor + "/" + engine["engine_type"] + "/" + engine["adapter_engine"]
                engines[key].append(value)
        frames = [
            f
            for f in graphics["frames"]
            if begin <= graphics["started_unix_s"] + f["elapsed_s"]
            and graphics["started_unix_s"] + f["elapsed_s"] + f["work_ms"] / 1000 <= end
        ]
        trials.append(
            {
                "round": trial["round"],
                "condition": trial["condition"],
                "throughput_tokens_per_second": stats(
                    [r["tokens_per_second_end_to_end"] for r in trial["inference"]]
                ),
                "time_to_first_content_ms": stats(
                    [r["time_to_first_content_ms"] for r in trial["inference"]]
                ),
                "placement_provider": graphics.get("placement_provider", "staged"),
                "retained_source_setup_upload_bytes": graphics.get("preparation_upload_bytes"),
                "live_host_uploaded_bytes_per_frame": stats(
                    [f["host_uploaded_bytes"] for f in frames if "host_uploaded_bytes" in f]
                ),
                "live_device_placement_copy_bytes_per_frame": stats(
                    [
                        f["device_placement_copy_bytes"]
                        for f in frames
                        if "device_placement_copy_bytes" in f
                    ]
                ),
                "frames_during_inference": len(frames),
                "stages_ms": {
                    name: stats([f[name] for f in frames])
                    for name in ("placement_ms", "upload_ms", "compose_ms", "present_ms", "work_ms")
                },
                "iterations": len(graphics["frames"]),
                "loop_rate": len(graphics["frames"]) / graphics["live_wall_seconds"],
                "skipped_slots": graphics["skipped_slots"],
                "deadline_misses": graphics["deadline_misses"],
                "cpu_core_equivalents": graphics["live_process_cpu_seconds"]
                / graphics["live_wall_seconds"],
                "screen_checks": graphics["screen_checks"],
                "end_screen_max_error": graphics["end_screen_max_error"],
                "complete_counter_intervals": len(selected),
                "rejected_engine_samples": rejected,
                "engine_percent": {key: stats(values) for key, values in sorted(engines.items())},
                "igcl": telemetry_interval(samples, begin, end, adapter["index"]),
            }
        )
    conditions = {}
    for condition in benchmark["conditions"]:
        rows = [t for t in trials if t["condition"] == condition]
        conditions[condition] = {
            "trial_median_throughput": stats(
                [t["throughput_tokens_per_second"]["median"] for t in rows]
            ),
            "trial_median_stages_ms": {
                name: stats([t["stages_ms"][name]["median"] for t in rows])
                for name in ("placement_ms", "upload_ms", "compose_ms", "present_ms", "work_ms")
            },
            "cpu_core_equivalents": stats([t["cpu_core_equivalents"] for t in rows]),
            "mean_gpu_clock_mhz": stats(
                [
                    t["igcl"]["gpu_clock_mhz"]["mean"]
                    for t in rows
                    if t["igcl"]["gpu_clock_mhz"]["mean"] is not None
                ]
            ),
            "iterations": sum(t["iterations"] for t in rows),
            "skipped_slots": sum(t["skipped_slots"] for t in rows),
            "deadline_misses": sum(t["deadline_misses"] for t in rows),
        }
    return {
        "scope": benchmark["scope"],
        "fps": benchmark["fps"],
        "comparisons": benchmark["comparisons"],
        "conditions": conditions,
        "trials": trials,
        "counter_capture": metadata,
        "gpu_luid": luid,
        "completion_token_counts": dict(
            Counter(r["completion_tokens"] for t in benchmark["trials"] for r in t["inference"])
        ),
        "output_hashes": dict(
            Counter(r["output_sha256"] for t in benchmark["trials"] for r in t["inference"])
        ),
        "notes": [
            "Only entire counter query intervals within inference are included.",
            "Each engine is retained separately; utilization percentages are not added.",
            "A Neural engine on the verified Arc GPU is GPU activity, not NPU activity.",
            "The outer desktop still uses DWM; scanout timing is not measured.",
            "Paired ratios and ratios of descriptive condition medians are different statistics.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_directory", type=Path)
    parser.add_argument("--benchmark", default="delegate-sustained-30.json")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_text(
        json.dumps(summarize(args.capture_directory, args.benchmark), indent=2, allow_nan=False)
        + "\n"
    )


if __name__ == "__main__":
    main()
