import json
import time

import numpy as np
import openvino as ov
from openvino import opset13 as op

core = ov.Core()
rng = np.random.default_rng(5010)
h, w = 360, 640
dh, dw = 720, 1280
image = rng.integers(0, 161, (1, 4, h, w), dtype=np.uint8).astype(np.float16)
image[:, 3] = 160
background = rng.integers(0, 256, (1, 4, dh, dw), dtype=np.uint8).astype(np.float16)
background[:, 3] = 255
sx = (np.arange(dw) + 0.5) * w / dw - 0.5
sy = (np.arange(dh) + 0.5) * h / dh - 0.5
ix = np.floor(sx).astype(int)
iy = np.floor(sy).astype(int)
wx = sx - ix
wy = sy - iy
sampled = np.zeros((1, 4, dh, dw), np.float64)
for dx, dy in [(0, 0), (1, 0), (0, 1), (1, 1)]:
    data = image[
        :, :, np.clip(iy + dy, 0, h - 1)[:, None], np.clip(ix + dx, 0, w - 1)[None, :]
    ].astype(np.float64)
    sampled += data * (wy if dy else 1 - wy)[:, None] * (wx if dx else 1 - wx)[None, :]
ref = sampled + background.astype(np.float64) * (1 - sampled[:, 3:4] / 255)
p = op.parameter(image.shape, np.float16)
b = op.parameter(background.shape, np.float16)
r = op.interpolate(
    p, np.array([dh, dw], np.int64), "linear_onnx", "sizes", axes=np.array([2, 3], np.int64)
)
a = op.gather(r, np.array([3], np.int64), np.array(1, np.int64))
out = op.add(r, op.multiply(b, op.subtract(np.float16(1), op.multiply(a, np.float16(1 / 255)))))
model = ov.Model([out], [p, b])
reports = []
for target in ["CPU", "GPU", "NPU"]:
    try:
        st = time.perf_counter()
        compiled = core.compile_model(model, target)
        compile_ms = (time.perf_counter() - st) * 1000
        q = compiled.create_infer_request()
        ts = []
        for _ in range(8):
            st = time.perf_counter()
            out = q.infer([image, background])[0].copy()
            ts.append((time.perf_counter() - st) * 1000)
        error = float(np.max(np.abs(out.astype(np.float64) - ref)))
        reports.append(
            {
                "device": target,
                "actual": compiled.get_property("EXECUTION_DEVICES"),
                "compile_ms": compile_ms,
                "infer_ms": ts,
                "max_byte_error_unrounded": error,
                "passed": error < 2,
            }
        )
    except Exception as e:
        reports.append({"device": target, "passed": False, "error": str(e)})
print(json.dumps(reports, indent=2))
