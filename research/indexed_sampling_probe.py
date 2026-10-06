"""Exploratory CPU/NPU sampling comparison; positional arguments specify image extents."""

import json
import sys
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op

core = ov.Core()
width, height = map(int, sys.argv[1:3]) if len(sys.argv) > 2 else (64, 64)
side = 32
n = side * side
rng = np.random.default_rng(5010)
image = rng.integers(0, 256, (1, 4, height, width), dtype=np.uint8).astype(np.float16) / np.float16(
    255
)
image[:, 3] = 1
indices = rng.integers(0, width * height, (4, n), dtype=np.int32)
weights = rng.random((1, 4, n), dtype=np.float32)
weights /= weights.sum(axis=1, keepdims=True)
weights = weights.astype(np.float16)
reference = (
    (image.reshape(4, -1)[:, indices].astype(np.float64) * weights.astype(np.float64))
    .sum(axis=1)
    .reshape(1, 4, side, side)
)
results = []
for target in ["CPU", "NPU"]:
    try:
        data = op.parameter(image.shape, np.float16)
        ix = op.parameter(indices.shape, np.int32)
        weight = op.parameter(weights.shape, np.float16)
        texels = op.gather(
            op.reshape(data, op.constant(np.array([4, width * height], np.int64)), False),
            ix,
            op.constant(1, np.int64),
        )
        sampled = op.reshape(
            op.reduce_sum(op.multiply(texels, weight), op.constant([1], np.int64), False),
            op.constant([1, 4, side, side], np.int64),
            False,
        )
        model = ov.Model([sampled], [data, ix, weight])
        start = time.perf_counter()
        compiled = core.compile_model(model, target)
        request = compiled.create_infer_request()
        times = []
        errors = []
        for _i in range(5):
            before = time.perf_counter()
            actual = request.infer([image, indices, weights])[0].copy()
            times.append((time.perf_counter() - before) * 1000)
            errors.append(float(np.max(np.abs(actual - reference))))
            indices = np.roll(indices, 1, axis=1).copy()
            reference = (
                (image.reshape(4, -1)[:, indices].astype(np.float64) * weights.astype(np.float64))
                .sum(axis=1)
                .reshape(1, 4, side, side)
            )
        results.append(
            {
                "device": target,
                "execution": compiled.get_property("EXECUTION_DEVICES"),
                "input": [width, height],
                "samples": n,
                "elapsed_including_compile": time.perf_counter() - start,
                "infer_ms": times,
                "errors": errors,
                "passed": max(errors) < 2 / 255,
            }
        )
    except Exception as e:
        results.append({"device": target, "passed": False, "error": str(e)})
print(json.dumps(results, indent=2))
