"""Isolate resize graph variants; timings/checksums only, not a pixel accuracy test."""

import json
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op

rng = np.random.default_rng(5010)
x = rng.integers(0, 161, (1, 4, 360, 640), dtype=np.uint8).astype(np.float16)
x[:, 3] = 160
b = rng.integers(0, 256, (1, 4, 720, 1280), dtype=np.uint8).astype(np.float16)
b[:, 3] = 255
core = ov.Core()
reports = []
for variant in ["base", "scalar_after", "rank4_after", "scalar_before", "rank4_before"]:
    p = op.parameter(x.shape, np.float16)
    bg = op.parameter(b.shape, np.float16)
    a = op.parameter([1, 1, 1, 1] if "rank4" in variant else [1], np.float16)
    r = op.interpolate(
        op.multiply(p, a) if "before" in variant else p,
        np.array([720, 1280], np.int64),
        "linear_onnx",
        "sizes",
        axes=np.array([2, 3], np.int64),
    )
    if "after" in variant:
        r = op.multiply(r, a)
    alpha = op.gather(r, np.array([3], np.int64), np.array(1, np.int64))
    out = op.add(
        r, op.multiply(bg, op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255))))
    )
    model = ov.Model([out], [p, bg] if variant == "base" else [p, bg, a])
    start = time.perf_counter()
    try:
        c = core.compile_model(model, "NPU")
        compile_ms = 1000 * (time.perf_counter() - start)
        q = c.create_infer_request()
        times = []
        for _ in range(8):
            start = time.perf_counter()
            out = q.infer(
                [x, b] if variant == "base" else [x, b, np.full(a.get_shape(), 1, np.float16)]
            )[0].copy()
            times.append(1000 * (time.perf_counter() - start))
        report = {
            "variant": variant,
            "actual": c.get_property("EXECUTION_DEVICES"),
            "compile_ms": compile_ms,
            "infer_ms": times,
            "sum": float(out.astype(np.float64).sum()),
        }
    except Exception as e:
        report = {"variant": variant, "error": str(e)}
    reports.append(report)
    print(json.dumps(report), flush=True)
