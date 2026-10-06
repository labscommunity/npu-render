import json

import pytest

from npu_render.benchmark import comparisons, trial_order
from npu_render.inference import consume_stream, sse_data


def stream(*events, done=True):
    lines = []
    for event in events:
        lines.extend([f"data: {json.dumps(event)}\r\n".encode(), b"\r\n"])
    if done:
        lines.extend([b"data: [DONE]\n", b"\n"])
    return lines


def test_usage_counts_tokens_while_chunk_timing_stays_explicit():
    ticks = iter([10.1, 10.3, 10.5])
    report = consume_stream(
        stream(
            {"choices": [{"delta": {"role": "assistant"}}]},
            {"choices": [{"delta": {"content": "hello world"}}], "n_tokens": 3},
            {"choices": [{"delta": {"content": " again"}, "finish_reason": "length"}]},
            {"choices": [], "usage": {"completion_tokens": 5, "prompt_tokens": 7}},
        ),
        10,
        clock=lambda: next(ticks),
    )
    assert report["content_chunks"] == 2
    assert report["completion_tokens"] == 5
    assert report["tokens_per_second_end_to_end"] == 10
    assert report["time_to_first_content_ms"] == pytest.approx(100)
    assert report["inter_content_chunk_ms"] == pytest.approx([200])
    assert report["chunk_declared_tokens"] == [3, None]


def test_missing_usage_never_turns_text_chunks_into_tokens():
    report = consume_stream(
        stream({"choices": [{"delta": {"content": "many words here"}}]}),
        0,
        clock=lambda: 1,
    )
    assert report["completion_tokens"] is None
    assert report["tokens_per_second_end_to_end"] is None


@pytest.mark.parametrize(
    "events",
    [
        stream({"error": {"message": "device failed"}}),
        stream({"choices": [{"delta": {"content": "partial"}}]}, done=False),
        stream({"choices": [{"delta": {}}]}),
    ],
)
def test_failed_or_truncated_requests_do_not_become_benchmark_results(events):
    with pytest.raises(RuntimeError):
        consume_stream(events, 0, clock=lambda: 1)


def test_sse_multiline_and_comments():
    assert list(sse_data([b": keepalive\n", b"data: first\n", b"data: second\n", b"\n"])) == [
        "first\nsecond"
    ]


def test_trial_order_balances_positions_over_first_cycle():
    conditions = ["GPU", "NPU", "CPU", "IDLE"]
    orders = [trial_order(i, conditions) for i in range(4)]
    for index in range(4):
        assert {order[index] for order in orders} == set(conditions)


def test_comparisons_require_matching_rounds_and_known_token_counts():
    trials = [
        {"round": 0, "condition": "GPU", "inference": [{"tokens_per_second_end_to_end": 10}]},
        {"round": 0, "condition": "NPU", "inference": [{"tokens_per_second_end_to_end": 12}]},
        {"round": 1, "condition": "NPU", "inference": [{"tokens_per_second_end_to_end": 99}]},
        {"round": 2, "condition": "GPU", "inference": [{"tokens_per_second_end_to_end": None}]},
        {"round": 2, "condition": "NPU", "inference": [{"tokens_per_second_end_to_end": 99}]},
    ]
    assert comparisons(trials)["paired_end_to_end_throughput_ratios"] == [
        {"round": 0, "npu_over_gpu": 1.2}
    ]
