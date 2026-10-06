import threading
from concurrent.futures import ThreadPoolExecutor

import numpy as np
import pytest

from npu_render import Composition, Renderer
from npu_render.openvino_backend import execution_matches
from npu_render.reference import qualification_inputs, source_over_reference


@pytest.mark.parametrize("layout", ["NCHW", "NHWC"])
@pytest.mark.parametrize("precision", ["f16", "f32"])
def test_cpu_composition_matches_independent_oracle(layout, precision):
    spec = Composition(31, 19, 3, precision, layout)
    renderer = Renderer(spec, "CPU")
    inputs = qualification_inputs(spec)
    frame = renderer.compose(inputs["background"], [inputs[n] for n in spec.input_names[1:]])
    assert frame.backend == "CPU"
    assert frame.execution_devices == ("CPU",)
    np.testing.assert_allclose(
        frame.pixels, source_over_reference(inputs, spec), atol=spec.tolerance, rtol=0
    )


def test_returned_pixels_survive_later_inference():
    spec = Composition(17, 13, precision="f32")
    renderer = Renderer(spec, "CPU")
    inputs = qualification_inputs(spec)
    first = renderer.compose(inputs["background"], [inputs["layer_0"]])
    saved = first.pixels.copy()
    inputs["layer_0"].fill(0)
    second = renderer.compose(inputs["background"], [inputs["layer_0"]])
    np.testing.assert_array_equal(first.pixels, saved)
    assert not np.shares_memory(first.pixels, second.pixels)
    assert not np.array_equal(first.pixels, second.pixels)


class ReferenceProgram:
    def __init__(self, spec, target, corrupt=False):
        self.spec = spec
        self.target = target
        self.execution_devices = (target,)
        self.corrupt = corrupt

    def execute(self, inputs):
        if self.corrupt:
            return np.full(self.spec.shape, np.nan, dtype=self.spec.dtype)
        return source_over_reference(inputs, self.spec).astype(self.spec.dtype)


class TestProvider:
    __test__ = False
    name = "test-reference"

    def __init__(self):
        self.calls = []
        self.fail = False
        self.corrupt = False
        self.started = threading.Event()
        self.release = threading.Event()
        self.block = False

    def devices(self):
        return ()

    def prepare(self, operation, target):
        self.calls.append(target)
        if target == "NPU":
            if self.fail:
                raise RuntimeError("device unavailable")
            if self.block:
                self.started.set()
                if not self.release.wait(5):
                    raise RuntimeError("test preparation timed out")
        return ReferenceProgram(operation, target, self.corrupt)


@pytest.mark.parametrize("failure", ["fail", "corrupt"])
def test_failed_switch_preserves_usable_old_backend(failure):
    spec = Composition(7, 5)
    provider = TestProvider()
    renderer = Renderer(spec, "GPU", provider=provider)
    setattr(provider, failure, True)
    with pytest.raises(RuntimeError):
        renderer.switch("NPU")
    assert renderer.backend == "GPU"
    inputs = qualification_inputs(spec)
    frame = renderer.compose(inputs["background"], [inputs["layer_0"]])
    assert frame.backend == "GPU"
    assert provider.calls == ["GPU", "NPU"]  # No hidden CPU retry.


def test_old_backend_renders_while_replacement_is_preparing():
    spec = Composition(7, 5)
    provider = TestProvider()
    renderer = Renderer(spec, "GPU", provider=provider)
    provider.block = True
    with ThreadPoolExecutor(max_workers=1) as executor:
        pending = executor.submit(renderer.switch, "NPU")
        try:
            assert provider.started.wait(5)
            inputs = qualification_inputs(spec)
            assert renderer.compose(inputs["background"], [inputs["layer_0"]]).backend == "GPU"
        finally:
            provider.release.set()
        assert pending.result(timeout=5) == "NPU"
    assert renderer.backend == "NPU"


def test_repeated_frames_and_noop_switch_do_not_recompile():
    spec = Composition(7, 5)
    provider = TestProvider()
    renderer = Renderer(spec, "CPU", provider=provider)
    inputs = qualification_inputs(spec)
    renderer.compose(inputs["background"], [inputs["layer_0"]])
    renderer.switch("cpu")
    renderer.compose(inputs["background"], [inputs["layer_0"]])
    assert provider.calls == ["CPU"]


@pytest.mark.parametrize(
    ("target", "actual", "expected"),
    [
        ("NPU", ("NPU",), True),
        ("NPU", ("CPU",), False),
        ("NPU", ("N", "P", "U"), False),
        ("GPU", ("GPU.0",), True),
        ("GPU.1", ("GPU.0",), False),
        ("GPU.1", ("GPU.1.0",), True),
        ("GPU", ("GPU", "CPU"), False),
        ("CPU", (), False),
    ],
)
def test_device_attribution_rejects_fallback_and_wrong_adapter(target, actual, expected):
    assert execution_matches(target, actual) is expected


def test_bad_surface_metadata_rejected_before_execution():
    spec = Composition(7, 5)
    renderer = Renderer(spec, "CPU", provider=TestProvider())
    inputs = qualification_inputs(spec)
    with pytest.raises(ValueError, match="foreground layers"):
        renderer.compose(inputs["background"], [])
    with pytest.raises(ValueError, match="dtype"):
        renderer.compose(inputs["background"].astype(np.float32), [inputs["layer_0"]])
    with pytest.raises(ValueError, match="contiguous"):
        renderer.compose(inputs["background"][..., ::-1], [inputs["layer_0"]])
