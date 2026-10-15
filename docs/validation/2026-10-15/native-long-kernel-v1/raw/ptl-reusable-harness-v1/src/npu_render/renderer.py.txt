"""A renderer-boundary switch with preparation before atomic activation."""

import threading
import time
from collections.abc import Sequence
from pathlib import Path

import numpy as np

from .config import normalize_backend, resolve_backend
from .contracts import BackendProvider, Composition, RenderedFrame
from .reference import qualify


class Renderer:
    def __init__(
        self,
        operation: Composition,
        backend: str | None = None,
        *,
        provider: BackendProvider | None = None,
        config: Path | None = None,
    ):
        if provider is None:
            from .openvino_backend import OpenVINOProvider

            provider = OpenVINOProvider()
        self.operation = operation
        self.provider = provider
        self._config = config
        self._frame_lock = threading.RLock()
        self._switch_lock = threading.Lock()
        self._program = None
        self.switch(resolve_backend(backend, config))

    @property
    def backend(self) -> str:
        with self._frame_lock:
            return self._program.target

    def switch(self, backend: str) -> str:
        """Call on a control thread: compilation/warmup may take seconds.

        Existing frames can execute during preparation. Failed preparation leaves
        the current program active. Switches serialize; activation waits for any
        current frame. Nothing compiles inside compose().
        """
        target = normalize_backend(backend)
        with self._switch_lock:
            with self._frame_lock:
                if self._program is not None and self._program.target == target:
                    return target
            candidate = self.provider.prepare(self.operation, target)
            qualify(candidate, self.operation)
            with self._frame_lock:
                self._program = candidate
        return target

    def reload_selection(self) -> str:
        return self.switch(resolve_backend(path=self._config))

    def compose(self, background: np.ndarray, foregrounds: Sequence[np.ndarray]) -> RenderedFrame:
        if len(foregrounds) != self.operation.layers:
            raise ValueError(f"expected {self.operation.layers} foreground layers")
        inputs = dict(zip(self.operation.input_names, (background, *foregrounds), strict=True))
        for name, array in inputs.items():
            if not isinstance(array, np.ndarray):
                raise ValueError(f"{name} must be a NumPy array")
            if array.shape != self.operation.shape or array.dtype != self.operation.dtype:
                raise ValueError(
                    f"{name} must have shape {self.operation.shape} "
                    f"and dtype {self.operation.dtype}"
                )
            if not array.flags.c_contiguous or not array.flags.writeable:
                raise ValueError(f"{name} must be contiguous and writable for buffer sharing")
        # Inputs must not be mutated concurrently while synchronous inference runs.
        with self._frame_lock:
            program = self._program
            start = time.perf_counter()
            pixels = program.execute(inputs)
            elapsed = (time.perf_counter() - start) * 1000
            return RenderedFrame(
                pixels, program.target, program.execution_devices, self.provider.name, elapsed
            )
