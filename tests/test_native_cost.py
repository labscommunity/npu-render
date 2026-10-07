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


def control_report():
    return {
        "diagnostic": "compute-only",
        "diagnostic_wait_ms": 0,
        "full_graphics_trace": False,
        "npu_alpha": "convolution",
        "initial_present_hresult": 0,
        "frames": [
            {"backend": "NPU", "presented": False, "composed": True, "present_hresult": None}
        ],
    }


def test_omitted_presentation_is_explicit_and_not_a_success_hresult():
    report = control_report()
    benchmark.validate_graphics_condition("NPU_ONLY", report)
    report["frames"][0]["present_hresult"] = 0
    with pytest.raises(RuntimeError, match="presentation result"):
        benchmark.validate_graphics_condition("NPU_ONLY", report)


@pytest.mark.parametrize(
    "field,value",
    [
        ("full_graphics_trace", True),
        ("diagnostic", "none"),
        ("diagnostic_wait_ms", 3),
        ("npu_alpha", "gather"),
        ("frames", []),
    ],
)
def test_diagnostic_coverage_or_graph_mismatch_rejected(field, value):
    report = control_report()
    report[field] = value
    with pytest.raises(RuntimeError):
        benchmark.validate_graphics_condition("NPU_ONLY", report)


def test_diagnostic_iterations_are_not_counted_as_display_submissions():
    graphics = {
        **control_report(),
        "started_unix_s": 0,
        "live_wall_seconds": 1,
        "live_process_cpu_seconds": 0.1,
        "skipped_slots": 0,
        "deadline_misses": 0,
    }
    graphics["frames"][0].update(
        elapsed_s=0.2, scene_ms=0, upload_ms=0, compose_ms=3, wait_ms=0, present_ms=0, work_ms=3
    )
    report = cost.summarize(
        {
            "status": "complete",
            "comparisons": {},
            "trials": [
                {
                    "round": 0,
                    "condition": "NPU_ONLY",
                    "graphics": graphics,
                    "inference_begin_unix_s": 0,
                    "inference_end_unix_s": 1,
                    "inference": [
                        {
                            "tokens_per_second_end_to_end": 10,
                            "completion_tokens": 10,
                            "output_sha256": "test",
                        }
                    ],
                }
            ],
        },
        [{"kind": "adapter", "index": 0, "status": 0}],
    )
    trial = report["trials"][0]
    assert trial["whole_live_iterations"] == trial["whole_live_compositions"] == 1
    assert trial["whole_live_presentations"] == trial["whole_live_submissions_per_second"] == 0
    assert trial["full_graphics_trace"] is False
