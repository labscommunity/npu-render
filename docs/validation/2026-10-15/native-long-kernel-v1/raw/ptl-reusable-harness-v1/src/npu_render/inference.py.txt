"""Measure a real Tahoma-compatible SSE endpoint without treating chunks as tokens."""

import hashlib
import json
import os
import time
import urllib.request
from collections.abc import Iterable


def sse_data(lines: Iterable[bytes]) -> Iterable[str]:
    data = []
    for line in lines:
        text = line.decode("utf-8").rstrip("\r\n")
        if not text:
            if data:
                yield "\n".join(data)
                data = []
        elif text.startswith("data:"):
            data.append(text[5:].removeprefix(" "))
    if data:
        yield "\n".join(data)


def consume_stream(lines: Iterable[bytes], start: float, clock=time.perf_counter) -> dict:
    content_chunks, chunk_times, chunk_token_counts = [], [], []
    usage = None
    done = False
    finish_reason = None
    for data in sse_data(lines):
        if data == "[DONE]":
            done = True
            break
        event = json.loads(data)
        if event.get("error"):
            raise RuntimeError(f"inference stream error: {event['error']}")
        if event.get("usage") is not None:
            usage = event["usage"]
        for choice in event.get("choices", []):
            if choice.get("index", 0) != 0:
                raise ValueError("benchmark expects exactly one completion")
            delta = choice.get("delta", {})
            text = delta.get("content") or delta.get("reasoning_content") or ""
            if text:
                content_chunks.append(text)
                chunk_times.append(clock() - start)
                chunk_token_counts.append(event.get("n_tokens"))
            finish_reason = choice.get("finish_reason") or finish_reason
    elapsed = clock() - start
    if not done:
        raise RuntimeError("inference stream ended without [DONE]")
    if not content_chunks:
        raise RuntimeError("inference produced no content")
    completion_tokens = usage.get("completion_tokens") if usage else None
    if completion_tokens is not None and (
        type(completion_tokens) is not int or completion_tokens <= 0
    ):
        raise ValueError("invalid server completion-token count")
    return {
        "elapsed_s": elapsed,
        "time_to_first_content_ms": chunk_times[0] * 1000,
        "completion_tokens": completion_tokens,
        "tokens_per_second_end_to_end": completion_tokens / elapsed if completion_tokens else None,
        "usage": usage,
        "content_chunks": len(content_chunks),
        "chunk_arrival_s": chunk_times,
        "chunk_declared_tokens": chunk_token_counts,
        "inter_content_chunk_ms": [
            (right - left) * 1000 for left, right in zip(chunk_times, chunk_times[1:], strict=False)
        ],
        "finish_reason": finish_reason,
        "output_sha256": hashlib.sha256("".join(content_chunks).encode()).hexdigest(),
        "timing_scope": "client content arrival; chunks may contain multiple tokens",
    }


class InferenceClient:
    def __init__(self, endpoint: str, model: str, prompt: str, max_tokens: int, timeout: float):
        if max_tokens < 1 or timeout <= 0:
            raise ValueError("max_tokens and timeout must be positive")
        self.endpoint = endpoint.rstrip("/") + "/chat/completions"
        self.timeout = timeout
        self.payload = {
            "model": model,
            "messages": [{"role": "user", "content": prompt}],
            "max_tokens": max_tokens,
            "temperature": 0,
            "stream": True,
            "stream_options": {"include_usage": True},
        }

    def request(self) -> dict:
        headers = {"Content-Type": "application/json", "Accept": "text/event-stream"}
        key = os.environ.get("NPU_RENDER_API_KEY")
        if key:
            headers["Authorization"] = "Bearer " + key
        request = urllib.request.Request(
            self.endpoint, data=json.dumps(self.payload).encode(), headers=headers, method="POST"
        )
        started = time.perf_counter()
        with urllib.request.urlopen(request, timeout=self.timeout) as response:
            if "text/event-stream" not in response.headers.get("Content-Type", ""):
                raise RuntimeError("endpoint did not return an SSE stream")
            return consume_stream(response, started)
