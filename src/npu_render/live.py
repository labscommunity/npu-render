"""Paced animated replay with asynchronous backend changes and honest timing."""

import math
import platform
import threading
import time
from collections.abc import Callable, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import asdict
from datetime import UTC, datetime
from importlib.metadata import version

import numpy as np

from .config import normalize_backend
from .presentation import NullPresenter, WindowPresenter
from .renderer import Renderer
from .scene import AnimatedScene, DisplayConverter


def distribution(values: Sequence[float]) -> dict:
    if not len(values):
        return {"count": 0}
    return {
        "count": len(values),
        "median": float(np.median(values)),
        "p95": float(np.percentile(values, 95)),
        "p99": float(np.percentile(values, 99)),
        "max": float(np.max(values)),
    }


def run_session(
    renderer: Renderer,
    presenter: WindowPresenter | NullPresenter,
    *,
    seconds: float = 30,
    fps: float = 60,
    sequence: Sequence[str] = (),
    switch_every: float = 10,
    stop: threading.Event | None = None,
    on_ready: Callable[[], None] | None = None,
    idle: bool = False,
) -> dict:
    if not math.isfinite(seconds) or seconds <= 0 or seconds > 86400:
        raise ValueError("seconds must be positive and at most 86400")
    if not math.isfinite(fps) or not 1 <= fps <= 1000:
        raise ValueError("fps must be between 1 and 1000")
    if not math.isfinite(switch_every) or switch_every <= 0:
        raise ValueError("switch_every must be positive")
    sequence = tuple(normalize_backend(item) for item in sequence)
    scene = AnimatedScene(renderer.operation)
    converter = DisplayConverter(renderer.operation)
    # Allocate, exercise conversion, and submit before starting measured replay.
    for _ in range(3):
        frame = renderer.compose(*scene.update(0))
        presenter.submit(converter.convert(frame.pixels))
    samples, switches = [], []
    next_switch = switch_every
    sequence_index = 0
    pending = None
    switch_record = None
    period = 1 / fps
    slot, skipped = 0, 0
    started = time.perf_counter()
    reason = "duration"
    caption = ""
    if on_ready:
        on_ready()
    with ThreadPoolExecutor(max_workers=1, thread_name_prefix="npu-render-switch") as control:
        while True:
            now = time.perf_counter()
            elapsed = now - started
            if elapsed >= seconds:
                break
            if stop is not None and stop.is_set():
                reason = "stop_event"
                break
            actions = presenter.events()
            if "quit" in actions:
                reason = "window_closed"
                break
            if pending is not None and pending.done():
                try:
                    switch_record["activated_backend"] = pending.result()
                    switch_record["status"] = "activated"
                except Exception as error:
                    switch_record["status"] = "failed"
                    switch_record["error"] = str(error)
                switch_record["completed_at_s"] = elapsed
                switches.append(switch_record)
                pending = None
            requested = next((a for a in reversed(actions) if a != "quit"), None)
            if sequence_index < len(sequence) and elapsed >= next_switch and pending is None:
                requested = sequence[sequence_index]
                sequence_index += 1
                next_switch += switch_every
            if requested is not None and pending is None and not idle:
                switch_record = {"requested": requested, "requested_at_s": elapsed}
                pending = control.submit(
                    renderer.reload_selection if requested == "reload" else renderer.switch,
                    *(() if requested == "reload" else (requested,)),
                )
            target = started + slot * period
            if now < target:
                time.sleep(min(target - now, 0.005))
                continue
            # Drop obsolete replay slots instead of building an unbounded queue.
            current_slot = max(slot, int((now - started) / period))
            skipped += current_slot - slot
            slot = current_slot
            before = time.perf_counter()
            if not idle:
                inputs = scene.update(slot * period)
                after_scene = time.perf_counter()
                frame = renderer.compose(*inputs)
                after_compose = time.perf_counter()
                rgb = converter.convert(frame.pixels)
                after_convert = time.perf_counter()
                presenter.submit(rgb)
                after_submit = time.perf_counter()
                samples.append(
                    {
                        "slot": slot,
                        "at_s": before - started,
                        "backend": frame.backend,
                        "execution_devices": frame.execution_devices,
                        "scene_ms": (after_scene - before) * 1000,
                        "compose_ms": (after_compose - after_scene) * 1000,
                        "convert_ms": (after_convert - after_compose) * 1000,
                        "submit_ms": (after_submit - after_convert) * 1000,
                        "work_ms": (after_submit - before) * 1000,
                        "schedule_lateness_ms": max(0, before - started - slot * period) * 1000,
                        "budget_exceeded": after_submit > started + (slot + 1) * period,
                    }
                )
            status = "IDLE" if idle else f"{frame.backend} ({','.join(frame.execution_devices)})"
            new_caption = f"npu-render | {status} | 1 GPU / 2 NPU / 3 CPU / R saved / Esc exit"
            if pending:
                new_caption += f" | preparing {switch_record['requested']}"
            if switches and switches[-1]["status"] == "failed":
                new_caption += " | switch failed; previous backend retained"
            if new_caption != caption:
                presenter.caption(new_caption)
                caption = new_caption
            slot += 1
        measured_seconds = time.perf_counter() - started
        if pending is not None:
            # Release safely after compilation; do not leave a detached control thread.
            try:
                switch_record["activated_backend"] = pending.result()
                switch_record["status"] = "activated_after_replay"
            except Exception as error:
                switch_record.update(status="failed", error=str(error))
            switch_record["completed_at_s"] = time.perf_counter() - started
            switches.append(switch_record)
    summary = {}
    for backend in sorted({item["backend"] for item in samples}):
        rows = [item for item in samples if item["backend"] == backend]
        summary[backend] = {
            **{
                key: distribution([row[key] for row in rows])
                for key in ("scene_ms", "compose_ms", "convert_ms", "submit_ms", "work_ms")
            },
            "processing_deadline_misses": sum(row["budget_exceeded"] for row in rows),
        }
    return {
        "schema_version": 1,
        "created_at": datetime.now(UTC).isoformat(),
        "package_version": version("npu-render"),
        "openvino_version": version("openvino"),
        "platform": platform.platform(),
        "operation": asdict(renderer.operation),
        "presentation": presenter.metadata,
        "display_conversion": converter.implementation,
        "scope": "application replay through submit; scanout and OS composition not measured",
        "fps_target": fps,
        "elapsed_s": measured_seconds,
        "idle": idle,
        "stop_reason": reason,
        "frames_submitted": len(samples),
        "skipped_replay_slots": skipped,
        "summary": summary,
        "switches": switches,
        "frames": samples,
    }
