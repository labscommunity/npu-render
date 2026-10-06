import numpy as np
import pytest

from npu_render import Composition, Renderer
from npu_render.live import run_session
from npu_render.presentation import NullPresenter, WindowPresenter
from npu_render.reference import source_over_reference, to_nchw
from npu_render.scene import AnimatedScene, DisplayConverter, encode_srgb


@pytest.mark.parametrize("layout", ["NCHW", "NHWC"])
def test_animation_is_repeatable_premultiplied_and_correct(layout):
    spec = Composition(37, 23, 3, layout=layout)
    scene = AnimatedScene(spec)
    background, layers = scene.update(1.25)
    saved = [layer.copy() for layer in layers]
    scene.update(2.5)
    assert any(not np.array_equal(a, b) for a, b in zip(saved, layers, strict=True))
    scene.update(1.25)
    for expected, actual in zip(saved, layers, strict=True):
        np.testing.assert_array_equal(actual, expected)
        rgba = to_nchw(actual, spec)
        assert np.all(rgba[:, :3] <= rgba[:, 3:])
        assert actual.flags.c_contiguous
    rendered = Renderer(spec, "CPU").compose(background, layers)
    inputs = dict(zip(spec.input_names, [background, *layers], strict=True))
    np.testing.assert_allclose(
        rendered.pixels, source_over_reference(inputs, spec), atol=spec.tolerance, rtol=0
    )


def test_fp16_transfer_table_matches_direct_conversion_for_every_finite_value():
    values = np.arange(65536, dtype=np.uint16).view(np.float16)
    finite = values[np.isfinite(values)]
    spec = Composition(len(finite), 1)
    pixels = np.ones(spec.shape, spec.dtype)
    pixels[0, :3, 0] = finite
    converted = DisplayConverter(spec).convert(pixels)
    expected = encode_srgb(finite)
    np.testing.assert_array_equal(converted[0, :, 0], expected)
    assert converted.shape == (1, len(finite), 3)
    assert converted.flags.c_contiguous


def test_native_conversion_rejects_wrong_dtype_and_short_buffers():
    native = pytest.importorskip("npu_render._pixels")
    table = np.arange(65536, dtype=np.uint16).astype(np.uint8)
    with pytest.raises(ValueError, match="native FP16"):
        native.rgba16_to_rgb8(np.ones((1, 4, 2, 2), np.uint16), table, 2, 2)
    with pytest.raises(ValueError, match="native FP16"):
        native.rgba16_to_rgb8(np.ones((1, 4, 2, 2), np.float16), table[:20], 2, 2)
    with pytest.raises(ValueError, match="extent"):
        native.rgba16_to_rgb8(np.ones(1, np.float16), table, -1, 2)


def test_native_and_numpy_display_paths_are_identical(monkeypatch):
    import npu_render.scene as scene_module

    pytest.importorskip("npu_render._pixels")
    spec = Composition(37, 23, 3)
    pixels = Renderer(spec, "CPU").compose(*AnimatedScene(spec).update(3.75)).pixels
    native = DisplayConverter(spec).convert(pixels)
    monkeypatch.setattr(scene_module, "rgba16_to_rgb8", None)
    fallback = DisplayConverter(spec).convert(pixels)
    np.testing.assert_array_equal(native, fallback)


def test_paced_replay_records_submission_without_claiming_scanout():
    renderer = Renderer(Composition(32, 24), "CPU")
    result = run_session(renderer, NullPresenter(), seconds=0.08, fps=60)
    assert result["frames_submitted"] > 0
    assert result["presentation"]["scanout_measured"] is False
    for row in result["frames"]:
        assert row["execution_devices"] == ("CPU",)
        assert row["work_ms"] == pytest.approx(
            sum(row[key] for key in ("scene_ms", "compose_ms", "convert_ms", "submit_ms"))
        )


def test_idle_has_no_measured_composition_calls():
    renderer = Renderer(Composition(16, 16), "CPU")
    report = run_session(renderer, NullPresenter(), seconds=0.02, idle=True)
    assert report["frames"] == []
    assert report["idle"]


def test_software_window_roundtrip_marks_dummy_as_offscreen(monkeypatch):
    pytest.importorskip("pygame")
    monkeypatch.setenv("SDL_VIDEODRIVER", "dummy")
    presenter = WindowPresenter(32, 24)
    try:
        rgb = np.zeros((24, 32, 3), np.uint8)
        rgb[5:15, 7:20] = [10, 80, 220]
        presenter.submit(rgb)
        actual = presenter.pygame.surfarray.array3d(presenter.surface).transpose(1, 0, 2)
        np.testing.assert_array_equal(actual, rgb)
        assert presenter.metadata["kind"] == "offscreen_test_window"
    finally:
        presenter.close()
