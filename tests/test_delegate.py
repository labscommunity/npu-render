import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    "delegate_benchmark", Path(__file__).parents[1] / "tools/benchmark_delegate.py"
)
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


def capture(mode="NPU"):
    return {
        "passed": True,
        "validation_only": False,
        "cancelled": False,
        "recovered_to_dwm": False,
        "session_id": 1,
        "initial_mode": mode,
        "fps": 60,
        "npu_graph": "source-over",
        "opaque_oracle_max_error": 0,
        "end_screen_max_error": 0,
        "pixel_qualification_cases": 8,
        "source_count": 3,
        "restored_source_windows": 3,
        "whole_desktop_offloaded": False,
        "npu_execution_device": "NPU",
        "npu_driver_version": "1005540",
        "screen_checks": [{"mode": name, "maximum_rgb_error": 0} for name in ("DWM", "NPU", "GPU")],
        "live_wall_seconds": 1 / 60,
        "move_calls": 3 if mode == "DWM" else 0,
        "frames": [
            {
                "mode": mode,
                "visible_source_windows": 3 if mode == "DWM" else 0,
                "npu_blends": 3 if mode == "NPU" else 0,
                "gpu_blends": 3 if mode == "GPU" else 0,
                "present_hresult": None if mode == "DWM" else 0,
            }
        ],
    }


@pytest.mark.parametrize("mode", ["DWM", "NPU", "GPU"])
def test_complete_cooperating_window_handoff(mode):
    benchmark.validate_capture(capture(mode), mode, 60, "1005540")


@pytest.mark.parametrize(
    "field,value",
    [
        ("opaque_oracle_max_error", 1),
        ("end_screen_max_error", 2),
        ("npu_graph", "opaque-select"),
        ("source_count", 2),
        ("restored_source_windows", 0),
        ("whole_desktop_offloaded", True),
        ("npu_execution_device", "CPU"),
        ("npu_driver_version", "other"),
        ("recovered_to_dwm", True),
        ("move_calls", 3),
        ("live_wall_seconds", float("nan")),
        ("live_wall_seconds", 0),
        ("live_wall_seconds", 1),
        ("frames", []),
        ("screen_checks", []),
    ],
)
def test_invalid_or_incomplete_offload_evidence_rejected(field, value):
    report = capture()
    report[field] = value
    with pytest.raises(RuntimeError):
        benchmark.validate_capture(report, "NPU", 60, "1005540")


@pytest.mark.parametrize(
    "field,value",
    [
        ("visible_source_windows", 3),
        ("npu_blends", 0),
        ("gpu_blends", 3),
        ("present_hresult", None),
    ],
)
def test_sources_must_be_retired_and_npu_must_execute(field, value):
    report = capture()
    report["frames"][0][field] = value
    with pytest.raises(RuntimeError):
        benchmark.validate_capture(report, "NPU", 60, "1005540")


def test_dwm_movement_is_not_reported_as_our_presentation():
    report = capture("DWM")
    report["frames"][0]["present_hresult"] = 0
    with pytest.raises(RuntimeError):
        benchmark.validate_capture(report, "DWM", 60, "1005540")


def test_comparisons_pair_round_medians_and_keep_unmatched_rounds_out():
    trials = [
        {"round": index, "condition": mode, "inference": [{"tokens_per_second_end_to_end": rate}]}
        for index, mode, rate in (
            (0, "DWM", 10),
            (0, "NPU", 8),
            (1, "DWM", 20),
            (1, "NPU", 18),
            (2, "NPU", 100),
        )
    ]
    result = benchmark.comparisons(trials)
    assert result["npu_over_dwm"]["median_ratio"] == pytest.approx(0.85)
    assert len(result["npu_over_dwm"]["paired_ratios"]) == 2
    assert result["gpu_over_dwm"]["median_ratio"] is None


def test_retained_sources_require_no_live_host_upload_and_device_placement():
    report = capture()
    report.update(
        placement_provider="retained",
        preparation_upload_bytes=100,
        source_width=426,
        source_height=240,
    )
    report["frames"][0].update(
        host_uploaded_bytes=0, device_placement_copy_bytes=3 * 1280 * 720 * 4
    )
    benchmark.validate_capture(report, "NPU", 60, "1005540", expected_placement="retained")
    report["frames"][0]["host_uploaded_bytes"] = 1280 * 720 * 4
    with pytest.raises(RuntimeError, match="host uploads"):
        benchmark.validate_capture(report, "NPU", 60, "1005540", expected_placement="retained")
    report["frames"][0].update(host_uploaded_bytes=0, device_placement_copy_bytes=0)
    with pytest.raises(RuntimeError, match="omitted device copies"):
        benchmark.validate_capture(report, "NPU", 60, "1005540", expected_placement="retained")


def test_staged_path_cannot_masquerade_as_retained():
    with pytest.raises(RuntimeError, match="requested interactive mode"):
        benchmark.validate_capture(capture(), "NPU", 60, "1005540", expected_placement="retained")
