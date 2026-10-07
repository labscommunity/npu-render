import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    "analyze_gpu_telemetry", Path(__file__).parents[1] / "tools" / "analyze_gpu_telemetry.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)

LUID = "luid_0x00000000_0x00000001"
ADAPTERS = [{"instance_luid": LUID, "description": "test GPU", "hardware": True}]


def sample(second, values, start="2026-10-07T00:00:00Z"):
    return {
        "at_utc": f"2026-10-07T01:00:{second:02d}Z",
        "processes": [{"pid": 10, "name": "dwm", "start_utc": start}],
        "engines": [
            {
                "pid": 10,
                "adapter_engine": f"{LUID}_phys_0_eng_{i}",
                "engine_type": "",
                "counter_status": status,
                "utilization_percent": value,
            }
            for i, (status, value) in enumerate(values)
        ],
    }


def test_independent_engines_are_not_added():
    report = module.summarize(
        [sample(1, [(0, 70), (1, 60)]), sample(2, [(0, 50), (0, 80)])], ADAPTERS
    )
    ranked = report["processes_by_busiest_mean_hardware_engine"]
    assert len(ranked) == 1
    assert ranked[0]["busiest_mean_engine"]["mean_sample_percent"] == 70
    assert ranked[0]["busiest_mean_engine"]["engine_type"] == ""


def test_bad_and_missing_readings_are_not_zero():
    report = module.summarize(
        [
            sample(1, [(0, 40)]),
            sample(2, [(0xC0000BBA, 0)]),
            sample(3, []),
            sample(4, [(0, float("nan"))]),
            sample(5, [(0, 101)]),
        ],
        ADAPTERS,
    )
    engine = report["engines"][0]
    assert engine["mean_sample_percent"] == 40
    assert engine["valid_samples"] == 1
    assert engine["observed_samples"] == 4
    assert engine["invalid_samples"] == 3
    assert report["invalid_counter_status_samples"] == 1


def test_pid_reuse_is_separate_and_unknown_identity_excluded():
    report = module.summarize(
        [
            sample(1, [(0, 10)]),
            sample(2, [(0, 90)], "2026-10-07T01:00:01Z"),
            sample(3, [(0, 50)], None),
        ],
        ADAPTERS,
    )
    assert len(report["processes_by_busiest_mean_hardware_engine"]) == 2
    assert report["unattributed_engine_samples"] == 1


def test_software_or_unknown_adapter_is_not_ranked_as_gpu():
    for adapters in ([], [{**ADAPTERS[0], "hardware": False}]):
        report = module.summarize([sample(1, [(0, 90)])], adapters)
        assert report["engines"][0]["mean_sample_percent"] == 90
        assert report["processes_by_busiest_mean_hardware_engine"] == []


def test_duplicate_or_unordered_samples_rejected():
    with pytest.raises(ValueError, match="ordered"):
        module.summarize([sample(2, []), sample(1, [])], ADAPTERS)
    duplicated = sample(1, [(0, 90)])
    duplicated["engines"] *= 2
    with pytest.raises(ValueError, match="duplicate"):
        module.summarize([duplicated], ADAPTERS)
