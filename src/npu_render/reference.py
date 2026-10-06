"""Independent numeric oracle and preparation-time qualification fixtures."""

from collections.abc import Mapping

import numpy as np

from .contracts import Composition, PreparedComposition


def to_nchw(array: np.ndarray, spec: Composition) -> np.ndarray:
    return array if spec.layout == "NCHW" else array.transpose(0, 3, 1, 2)


def from_nchw(array: np.ndarray, spec: Composition) -> np.ndarray:
    result = array if spec.layout == "NCHW" else array.transpose(0, 2, 3, 1)
    return np.ascontiguousarray(result, dtype=spec.dtype)


def source_over_reference(inputs: Mapping[str, np.ndarray], spec: Composition) -> np.ndarray:
    # Closed-form visibility weights, independent of the graph's forward recurrence.
    background = to_nchw(inputs["background"], spec).astype(np.float64)
    transmittance = np.ones((1, 1, spec.height, spec.width), dtype=np.float64)
    result = np.zeros_like(background)
    for i in reversed(range(spec.layers)):
        rgba = to_nchw(inputs[f"layer_{i}"], spec).astype(np.float64)
        result += rgba * transmittance
        transmittance *= 1 - rgba[:, 3:4]
    result += background * transmittance
    return result if spec.layout == "NCHW" else result.transpose(0, 2, 3, 1)


def qualification_inputs(spec: Composition) -> dict[str, np.ndarray]:
    rng = np.random.default_rng(20261005)
    inputs = {}
    for name in spec.input_names:
        rgba = rng.random((1, 4, spec.height, spec.width)).astype(spec.dtype)
        if name == "background":
            rgba[:, 3] = 1
        else:
            rgba[:, :3] *= rgba[:, 3:4]
        inputs[name] = from_nchw(rgba, spec)
    return inputs


def qualify(program: PreparedComposition, spec: Composition) -> None:
    """Warm and check the exact compiled shape before accepting a backend switch."""
    inputs = qualification_inputs(spec)
    for case in ("fractional", "transparent", "opaque_front"):
        if case == "transparent":
            for name in spec.input_names[1:]:
                inputs[name].fill(0)
        elif case == "opaque_front":
            front = to_nchw(inputs[spec.input_names[-1]], spec)
            front[:, :3] = 0.25
            front[:, 3] = 1
        expected = source_over_reference(inputs, spec)
        output = program.execute(inputs)
        if output.shape != spec.shape or not np.isfinite(output).all():
            raise RuntimeError(f"{program.target} failed {case}: invalid output")
        error = float(np.max(np.abs(output.astype(np.float64) - expected)))
        if error > spec.tolerance:
            raise RuntimeError(f"{program.target} failed {case}: error {error} > {spec.tolerance}")
