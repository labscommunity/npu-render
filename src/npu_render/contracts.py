"""Application-facing data contracts; independent of the device runtime."""

from collections.abc import Mapping
from dataclasses import dataclass
from typing import Protocol

import numpy as np


@dataclass(frozen=True)
class Composition:
    width: int
    height: int
    layers: int = 1
    precision: str = "f16"
    layout: str = "NCHW"

    def __post_init__(self):
        for name in ("width", "height", "layers"):
            value = getattr(self, name)
            if type(value) is not int or value < 1:
                raise ValueError(f"{name} must be a positive integer")
        if self.precision not in {"f16", "f32"}:
            raise ValueError("precision must be f16 or f32")
        if self.layout not in {"NCHW", "NHWC"}:
            raise ValueError("layout must be NCHW or NHWC")

    @property
    def shape(self) -> tuple[int, ...]:
        if self.layout == "NCHW":
            return (1, 4, self.height, self.width)
        return (1, self.height, self.width, 4)

    @property
    def dtype(self) -> np.dtype:
        return np.dtype(np.float16 if self.precision == "f16" else np.float32)

    @property
    def input_names(self) -> tuple[str, ...]:
        return ("background", *(f"layer_{i}" for i in range(self.layers)))

    @property
    def tolerance(self) -> float:
        # An exploratory numeric gate, not graphics API/display conformance.
        return 2 / 255 if self.precision == "f16" else 1e-5


@dataclass(frozen=True)
class Device:
    id: str
    name: str
    provider: str


@dataclass(frozen=True)
class RenderedFrame:
    """Owned pixels remain valid after later renders and backend switches."""

    pixels: np.ndarray
    backend: str
    execution_devices: tuple[str, ...]
    provider: str
    host_call_ms: float


class PreparedComposition(Protocol):
    target: str
    execution_devices: tuple[str, ...]

    def execute(self, inputs: Mapping[str, np.ndarray]) -> np.ndarray:
        """Execute synchronously and return an owned output array."""
        ...


class BackendProvider(Protocol):
    """A provider owns compilation and execution, never display presentation."""

    name: str

    def devices(self) -> tuple[Device, ...]: ...

    def prepare(self, operation: Composition, target: str) -> PreparedComposition: ...
