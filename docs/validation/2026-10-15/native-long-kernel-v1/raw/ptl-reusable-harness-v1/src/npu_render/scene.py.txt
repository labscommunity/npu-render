"""A repeatable animated workload and explicit linear-RGB display conversion."""

import math

import numpy as np

try:
    from ._pixels import rgba16_to_rgb8
except ImportError:
    rgba16_to_rgb8 = None

from .contracts import Composition
from .reference import from_nchw, to_nchw


class AnimatedScene:
    """Moving premultiplied surfaces; time is supplied by the replay clock."""

    def __init__(self, spec: Composition):
        self.spec = spec
        y, x = np.mgrid[: spec.height, : spec.width]
        background = np.ones((1, 4, spec.height, spec.width), dtype=spec.dtype)
        background[0, 0] = 0.03 + 0.12 * x / spec.width
        background[0, 1] = 0.04 + 0.12 * y / spec.height
        background[0, 2] = 0.12
        self.background = from_nchw(background, spec)
        self.layers = [np.zeros(spec.shape, spec.dtype) for _ in range(spec.layers)]

    def update(self, seconds: float) -> tuple[np.ndarray, list[np.ndarray]]:
        w, h = self.spec.width, self.spec.height
        rw, rh = max(1, w // 2), max(1, h // 2)
        colors = ((0.1, 0.65, 0.9), (0.85, 0.15, 0.2), (0.25, 0.8, 0.3))
        for i, layer in enumerate(self.layers):
            layer.fill(0)
            x = round((w - rw) * (0.5 + 0.45 * math.sin(seconds * 0.8 + i * 1.7)))
            y = round((h - rh) * (0.5 + 0.45 * math.cos(seconds * 0.6 + i * 1.1)))
            rgba = to_nchw(layer, self.spec)
            alpha = 0.65
            rgba[0, 3, y : y + rh, x : x + rw] = alpha
            for channel, color in enumerate(colors[i % len(colors)]):
                rgba[0, channel, y : y + rh, x : x + rw] = alpha * color
        return self.background, self.layers


def encode_srgb(linear: np.ndarray) -> np.ndarray:
    rgb = np.clip(linear.astype(np.float32), 0, 1)
    encoded = np.where(rgb <= 0.0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - 0.055)
    return np.rint(encoded * 255).astype(np.uint8)


class DisplayConverter:
    """CPU conversion to opaque RGB8. FP16 uses a precomputed transfer table."""

    def __init__(self, spec: Composition):
        self.spec = spec
        self.table = None
        self.implementation = "numpy_cpu"
        if spec.precision == "f16":
            values = np.arange(65536, dtype=np.uint16).view(np.float16)
            values = np.nan_to_num(values, nan=0, posinf=1, neginf=0)
            self.table = encode_srgb(values)
            if rgba16_to_rgb8 is not None and spec.layout == "NCHW":
                self.implementation = "native_cpu"

    def convert(self, pixels: np.ndarray) -> np.ndarray:
        # The workload has an opaque background. General alpha presentation is
        # deliberately not inferred from this opaque demo's conversion.
        if self.implementation == "native_cpu":
            data = rgba16_to_rgb8(pixels, self.table, self.spec.width, self.spec.height)
            return np.frombuffer(data, dtype=np.uint8).reshape(self.spec.height, self.spec.width, 3)
        rgb = to_nchw(pixels, self.spec)[0, :3]
        encoded = self.table[rgb.view(np.uint16)] if self.table is not None else encode_srgb(rgb)
        return np.ascontiguousarray(encoded.transpose(1, 2, 0))
