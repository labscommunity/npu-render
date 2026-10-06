"""Exact 2x half-pixel downscale equivalence: interpolate versus 2x2 average."""

import json
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op

core = ov.Core()
for variant in ["interpolate", "average_pool"]:
    image = op.parameter([1, 4, 720, 1280], np.float16)
    background = op.parameter([1, 4, 360, 640], np.float16)
    opacity = op.parameter([1], np.float16)
    if variant == "interpolate":
        resized = op.interpolate(
            image, np.array([360, 640]), "linear_onnx", "sizes", axes=np.array([2, 3])
        )
    else:
        resized = op.avg_pool(image, [2, 2], [0, 0], [0, 0], [2, 2], True)
    foreground = op.multiply(resized, opacity)
    alpha = op.gather(foreground, np.array([3]), np.array(1))
    result = op.add(
        foreground,
        op.multiply(
            background,
            op.subtract(np.float16(1), op.multiply(alpha, np.float16(1 / 255))),
        ),
    )
    model = ov.Model([result], [image, background, opacity])
    report = {"variant": variant}
    try:
        start = time.perf_counter()
        compiled = core.compile_model(model, "NPU")
        report["compile_ms"] = 1000 * (time.perf_counter() - start)
        report["actual"] = compiled.get_property("EXECUTION_DEVICES")
        request = compiled.create_infer_request()
        rng = np.random.default_rng(5010)
        times, maximum = [], 0.0
        for frame in range(8):
            source = rng.integers(0, 256, image.get_shape(), dtype=np.uint8).astype(np.float16)
            source[:, :3] = np.minimum(source[:, :3], source[:, 3:4])
            bg = rng.integers(0, 256, background.get_shape(), dtype=np.uint8).astype(np.float16)
            bg[:, 3] = 255
            a = np.array([1 if frame % 2 else 0.5], np.float16)
            # The independent half-pixel reference evaluates the four neighbors.
            ref = (
                sum(source[:, :, dy::2, dx::2].astype(np.float64) for dy in [0, 1] for dx in [0, 1])
                / 4
            )
            ref *= float(a[0])
            ref += bg.astype(np.float64) * (1 - ref[:, 3:4] / 255)
            start = time.perf_counter()
            observed = request.infer([source, bg, a])[0].copy()
            times.append(1000 * (time.perf_counter() - start))
            maximum = max(maximum, float(np.abs(observed.astype(np.float64) - ref).max()))
        report.update(infer_copy_ms=times, max_unrounded_byte_error=maximum, passed=maximum <= 2)
    except Exception as error:
        report.update(passed=False, error=str(error))
    print(json.dumps(report), flush=True)
