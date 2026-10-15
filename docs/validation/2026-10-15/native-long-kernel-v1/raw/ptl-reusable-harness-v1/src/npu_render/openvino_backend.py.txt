"""First provider: supported OpenVINO graph execution with explicit device routing."""

from collections.abc import Mapping

import numpy as np
import openvino as ov
from openvino import opset13 as op

from .config import normalize_backend
from .contracts import Composition, Device


def execution_matches(target: str, actual: tuple[str, ...]) -> bool:
    if not actual:
        return False
    if "." in target:
        return all(device == target or device.startswith(target + ".") for device in actual)
    return all(device.split(".")[0] == target for device in actual)


def build_composition(spec: Composition) -> ov.Model:
    params = []
    for name in spec.input_names:
        node = op.parameter(spec.shape, dtype=spec.dtype.type, name=name)
        node.output(0).get_tensor().set_names({name})
        params.append(node)
    result = params[0]
    axis = 1 if spec.layout == "NCHW" else 3
    for foreground in params[1:]:
        alpha = op.gather(foreground, op.constant([3], np.int64), op.constant(axis, np.int64))
        result = op.add(
            foreground,
            op.multiply(op.subtract(op.constant(1, spec.dtype.type), alpha), result),
        )
    return ov.Model([result], params, "source_over")


class OpenVINOComposition:
    def __init__(self, compiled: ov.CompiledModel, target: str):
        self.target = target
        actual = compiled.get_property("EXECUTION_DEVICES")
        self.execution_devices = (actual,) if isinstance(actual, str) else tuple(actual)
        if not execution_matches(target, self.execution_devices):
            raise RuntimeError(f"requested {target}, runtime reported {self.execution_devices}")
        self._request = compiled.create_infer_request()

    def execute(self, inputs: Mapping[str, np.ndarray]) -> np.ndarray:
        # Owned outputs avoid exposing storage that a later inference overwrites.
        # Shared Python inputs do not imply zero-copy device memory.
        outputs = self._request.infer(dict(inputs), share_inputs=True, share_outputs=False)
        return next(iter(outputs.values()))


class OpenVINOProvider:
    name = "openvino"

    def __init__(self, core: ov.Core | None = None):
        self._core = core if core is not None else ov.Core()

    def devices(self) -> tuple[Device, ...]:
        return tuple(
            Device(device, str(self._core.get_property(device, "FULL_DEVICE_NAME")), self.name)
            for device in self._core.available_devices
        )

    def prepare(self, operation: Composition, target: str) -> OpenVINOComposition:
        target = normalize_backend(target)
        config = {}
        if target.split(".")[0] in {"CPU", "GPU"}:
            config["INFERENCE_PRECISION_HINT"] = operation.precision
        compiled = self._core.compile_model(build_composition(operation), target, config)
        return OpenVINOComposition(compiled, target)
