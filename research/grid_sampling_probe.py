"""Exploratory CPU/NPU sampling comparison; positional arguments specify image extents."""

import json
import sys
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op

width, height = map(int, sys.argv[1:3]) if len(sys.argv) > 2 else (64, 64)
side = int(sys.argv[3]) if len(sys.argv) > 3 else 32
core = ov.Core()
rng = np.random.default_rng(5010)
image = rng.integers(0, 256, (1, 4, height, width), dtype=np.uint8).astype(np.float32) / 255
image[:, 3] = 1
x = rng.uniform(0, width - 1, (side, side))
y = rng.uniform(0, height - 1, (side, side))
grid = np.stack([(x + 0.5) * 2 / width - 1, (y + 0.5) * 2 / height - 1], axis=-1)[None]
reports = []
for dtype in [np.float16, np.float32]:
    try:
        im = image.astype(dtype)
        gr = grid.astype(dtype)
        # Oracle uses the supplied grid's actual precision, not an idealized grid.
        sx = (gr[0, :, :, 0].astype(np.float64) + 1) * width / 2 - 0.5
        sy = (gr[0, :, :, 1].astype(np.float64) + 1) * height / 2 - 0.5
        x0 = np.floor(sx).astype(int)
        y0 = np.floor(sy).astype(int)
        wx = sx - x0
        wy = sy - y0
        ref = np.zeros((1, 4, side, side), np.float64)
        for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]:
            cx = np.clip(x0 + dx, 0, width - 1)
            cy = np.clip(y0 + dy, 0, height - 1)
            valid = (x0 + dx >= 0) & (x0 + dx < width) & (y0 + dy >= 0) & (y0 + dy < height)
            ref += im[:, :, cy, cx].astype(np.float64) * (
                (wx if dx else 1 - wx) * (wy if dy else 1 - wy) * valid
            )
        data = op.parameter(im.shape, dtype)
        g = op.parameter(gr.shape, dtype)
        model = ov.Model(
            [
                op.grid_sample(
                    data, g, {"mode": "bilinear", "padding_mode": "zeros", "align_corners": False}
                )
            ],
            [data, g],
        )
        for target in ["CPU", "NPU"]:
            try:
                st = time.perf_counter()
                comp = core.compile_model(model, target)
                q = comp.create_infer_request()
                ts = []
                for _i in range(5):
                    a = time.perf_counter()
                    out = q.infer([im, gr])[0].copy()
                    ts.append((time.perf_counter() - a) * 1000)
                reports.append(
                    {
                        "dtype": np.dtype(dtype).name,
                        "device": target,
                        "actual": comp.get_property("EXECUTION_DEVICES"),
                        "image": [width, height],
                        "output": [side, side],
                        "max_error": float(abs(out - ref).max()),
                        "infer_ms": ts,
                        "passed": float(abs(out - ref).max()) < 2 / 255,
                    }
                )
            except Exception as e:
                reports.append(
                    {
                        "dtype": np.dtype(dtype).name,
                        "device": target,
                        "error": str(e),
                        "passed": False,
                    }
                )
    except Exception as e:
        reports.append({"error": str(e), "passed": False})
print(json.dumps(reports, indent=2))
