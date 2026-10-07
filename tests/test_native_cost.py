import copy
import importlib.util
from pathlib import Path

import pytest

ROOT = Path(__file__).parents[1]


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, ROOT / path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


cost = load("native_cost", "research/analyze_native_cost.py")
benchmark = load("native_benchmark", "tools/benchmark_native.py")


def sample(at, joules, **extra):
    return {
        "kind": "sample",
        "index": 0,
        "unix_s": at,
        "v2_status": 1,
        "v1_status": 0,
        "timeStamp": {"supported": True, "units": 7, "value": at + 100},
        "gpuEnergyCounter": {"supported": True, "units": 6, "value": joules},
        **extra,
    }


def test_energy_delta_and_complete_phase_intervals():
    rows = [sample(0, 0), sample(1, 10), sample(2, 40), sample(3, 90)]
    report = cost.telemetry_interval(rows, 0.5, 2.5, 0)
    assert report["adjacent_intervals"] == 1
    assert report["gpu_energy_delta_watts"]["mean"] == 30
    assert report["gpu_clock_mhz"]["count"] == 0
    assert report["gpu_clock_mhz"]["mean"] is None


def test_invalid_energy_samples_cannot_bridge_a_gap():
    rows = [sample(0, 0), sample(1, 100, v1_status=1), sample(2, 200)]
    report = cost.telemetry_interval(rows, 0, 2, 0)
    assert report["gpu_energy_delta_watts"]["count"] == 0


@pytest.mark.parametrize("change", ["units", "unsupported", "reset", "adapter", "clock", "nan"])
def test_invalid_deltas_are_not_power_values(change):
    left, right = sample(0, 10), sample(1, 20)
    if change == "units":
        right["gpuEnergyCounter"]["units"] = 4
    elif change == "unsupported":
        right["gpuEnergyCounter"]["supported"] = False
    elif change == "reset":
        right["gpuEnergyCounter"]["value"] = 1
    elif change == "adapter":
        right["index"] = 1
    elif change == "clock":
        right["timeStamp"]["value"] = 100
    else:
        right["gpuEnergyCounter"]["value"] = float("nan")
    assert cost.counter_rate(left, right, "gpuEnergyCounter", 6) is None


def test_out_of_order_or_duplicate_telemetry_rejected():
    for rows in ([sample(1, 1), sample(0, 0)], [sample(0, 0), sample(0, 0)]):
        with pytest.raises(ValueError, match="ordered"):
            cost.telemetry_interval(rows, 0, 3, 0)


def test_graph_condition_is_explicit_and_ratios_are_paired():
    assert benchmark.parse_conditions("gpu, npu_conv") == ["GPU", "NPU_CONV"]
    assert benchmark.condition_backend("NPU_CONV") == "NPU"
    for value in ("", "NPU,NPU", "AUTO"):
        with pytest.raises(ValueError):
            benchmark.parse_conditions(value)
    trials = [
        {
            "round": index,
            "condition": condition,
            "inference": [{"tokens_per_second_end_to_end": v} for v in values],
        }
        for index, condition, values in (
            (0, "GPU", [9, 10, 100]),
            (0, "NPU_CONV", [11, 12, 15]),
            (1, "GPU", [20, 30, 40]),
            (1, "NPU_CONV", [11, 15, 18]),
            (2, "NPU_CONV", [99]),
        )
    ]
    before = copy.deepcopy(trials)
    report = benchmark.native_comparisons(trials)
    assert report["npu_conv_over_gpu"]["paired_ratios"] == [
        {"round": 0, "ratio": 1.2},
        {"round": 1, "ratio": 0.5},
    ]
    assert report["npu_conv_over_gpu"]["ratio_distribution"]["median"] == 0.85
    assert trials == before
