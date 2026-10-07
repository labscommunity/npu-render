"""Rank observed Windows GPU engines without summing unrelated engine percentages."""

import argparse
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from datetime import datetime
from pathlib import Path

ENGINE = re.compile(r"^(luid_0x[0-9a-f]+_0x[0-9a-f]+)_phys_(\d+)_eng_(\d+)$", re.I)


def read_json(path):
    data = Path(path).read_bytes()
    return json.loads(
        data.decode("utf-16" if data.startswith((b"\xff\xfe", b"\xfe\xff")) else "utf-8-sig")
    )


def summarize(samples, adapters):
    if not samples:
        raise ValueError("capture has no samples")
    times = [datetime.fromisoformat(sample["at_utc"].replace("Z", "+00:00")) for sample in samples]
    if any(t.tzinfo is None for t in times) or any(
        b <= a for a, b in zip(times, times[1:], strict=False)
    ):
        raise ValueError("sample timestamps must be ordered, unique and timezone-aware")
    adapter_map = {a["instance_luid"].lower(): a for a in adapters if a.get("instance_luid")}
    groups = {}
    rejected = Counter()
    all_statuses = Counter()
    unknown_process_samples = 0
    for sample in samples:
        processes = {p["pid"]: p for p in sample.get("processes", [])}
        seen = set()
        for engine in sample["engines"]:
            all_statuses[str(engine.get("counter_status"))] += 1
            match = ENGINE.fullmatch(engine["adapter_engine"])
            if not match:
                rejected["unknown_engine_identifier"] += 1
                continue
            pid = engine["pid"]
            identity = (pid, engine["adapter_engine"].lower())
            if identity in seen:
                raise ValueError("duplicate process/engine in one sample")
            seen.add(identity)
            process = processes.get(pid, {})
            start = process.get("start_utc")
            name = process.get("name")
            if not start or not name:
                # Do not merge unknown lifetimes, or assign an old counter to a reused PID.
                unknown_process_samples += 1
                continue
            key = (*identity, start)
            luid, physical, index = match.groups()
            group = groups.setdefault(
                key,
                {
                    "pid": pid,
                    "process": name,
                    "process_start_utc": start,
                    "adapter_luid": luid.lower(),
                    "adapter": adapter_map.get(luid.lower(), {}).get("description"),
                    "hardware": adapter_map.get(luid.lower(), {}).get("hardware"),
                    "physical_adapter_index": int(physical),
                    "engine_index": int(index),
                    "engine_type": engine["engine_type"],
                    "observed_samples": 0,
                    "invalid_samples": 0,
                    "values": [],
                },
            )
            group["observed_samples"] += 1
            value = engine["utilization_percent"]
            reason = None
            if engine.get("counter_status") not in (0, 1):
                reason = "invalid_pdh_status"
            elif not isinstance(value, (float, int)) or not math.isfinite(value):
                reason = "nonfinite_or_missing_value"
            elif not 0 <= value <= 100:
                reason = "percentage_out_of_range"
            if reason:
                rejected[reason] += 1
                group["invalid_samples"] += 1
            else:
                group["values"].append(value)
    engines = []
    for group in groups.values():
        values = group.pop("values")
        group.update(
            valid_samples=len(values),
            nonzero_samples=sum(v > 0 for v in values),
            mean_sample_percent=statistics.fmean(values) if values else None,
            median_sample_percent=statistics.median(values) if values else None,
            peak_sample_percent=max(values) if values else None,
        )
        engines.append(group)
    engines.sort(key=lambda e: e["mean_sample_percent"] or 0, reverse=True)
    processes = defaultdict(list)
    for engine in engines:
        processes[(engine["pid"], engine["process_start_utc"])].append(engine)
    ranked = []
    for group in processes.values():
        valid = [e for e in group if e["valid_samples"] and e["hardware"] is True]
        if valid:
            busiest = max(valid, key=lambda e: e["mean_sample_percent"])
            ranked.append(
                {
                    "process": busiest["process"],
                    "pid": busiest["pid"],
                    "busiest_mean_engine": busiest,
                }
            )
    ranked.sort(key=lambda p: p["busiest_mean_engine"]["mean_sample_percent"], reverse=True)
    return {
        "schema_version": 1,
        "scope": "observed per-process/per-engine GPU counters; no offload or Tahoma benefit claim",
        "sample_count": len(samples),
        "first_sample_utc": samples[0]["at_utc"],
        "last_sample_utc": samples[-1]["at_utc"],
        "timestamp_span_seconds": (times[-1] - times[0]).total_seconds(),
        "rejected_values": dict(rejected),
        "all_counter_status_counts": dict(all_statuses),
        "invalid_counter_status_samples": sum(
            count for status, count in all_statuses.items() if status not in ("0", "1")
        ),
        "unattributed_engine_samples": unknown_process_samples,
        "processes_by_busiest_mean_hardware_engine": ranked,
        "engines": engines,
        "notes": [
            "Sample means describe observed intervals, not uninterrupted wall-clock utilization.",
            "Missing instances/invalid readings are not zero; short-lived processes may be missed.",
            "PID and process start time identify a lifetime; missing identity is excluded.",
            "Adapters must be enumerated during the same boot/device configuration as the capture.",
            "Ranking uses each process's busiest mean hardware engine, not a sum or "
            "Task Manager's instantaneous maximum.",
            "Blank engine types are retained; software/unknown adapters are not hardware-ranked.",
            "Zero readings alone do not establish idleness; verify with a positive control.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture", type=Path)
    parser.add_argument("--adapters", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    samples = [
        json.loads(line)
        for line in args.capture.read_text(encoding="utf-8-sig").splitlines()
        if line
    ]
    report = summarize(samples, read_json(args.adapters)["adapters"])
    args.output.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(
        json.dumps(
            {
                "samples": len(samples),
                "processes": len(report["processes_by_busiest_mean_hardware_engine"]),
                "rejected": report["rejected_values"],
            }
        )
    )


if __name__ == "__main__":
    main()
