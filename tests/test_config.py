import json

import pytest

from npu_render import Composition, Renderer
from npu_render.cli import main
from npu_render.config import normalize_backend, resolve_backend, save_backend


def test_config_precedence_and_atomic_replacement(tmp_path, monkeypatch):
    path = tmp_path / "settings" / "config.json"
    monkeypatch.delenv("NPU_RENDER_BACKEND", raising=False)
    assert resolve_backend(path=path) == "GPU"
    save_backend("npu", path)
    assert resolve_backend(path=path) == "NPU"
    monkeypatch.setenv("NPU_RENDER_BACKEND", "cpu")
    assert resolve_backend(path=path) == "CPU"
    assert resolve_backend("gpu", path) == "GPU"
    save_backend("gpu.0", path)
    assert json.loads(path.read_text())["backend"] == "GPU.0"
    assert list(path.parent.iterdir()) == [path]


@pytest.mark.parametrize("backend", ["AUTO", "HETERO:NPU,CPU", "NPU,CPU", "NPU.missing", ""])
def test_ambiguous_backend_policies_are_rejected(backend):
    with pytest.raises(ValueError):
        normalize_backend(backend)


def test_invalid_config_does_not_silently_choose_gpu(tmp_path, monkeypatch):
    monkeypatch.delenv("NPU_RENDER_BACKEND", raising=False)
    path = tmp_path / "config.json"
    path.write_text('{"version": 99, "backend": "CPU"}')
    with pytest.raises(ValueError, match="unsupported"):
        resolve_backend(path=path)
    path.write_text("broken json")
    with pytest.raises(ValueError):
        resolve_backend(path=path)


def test_cli_select_and_demo_use_saved_backend(tmp_path, monkeypatch):
    monkeypatch.delenv("NPU_RENDER_BACKEND", raising=False)
    config = tmp_path / "config.json"
    assert main(["select", "cpu", "--config", str(config)]) == 0
    renderer = Renderer(Composition(5, 3), config=config)
    assert renderer.backend == "CPU"
    image = tmp_path / "frame.ppm"
    assert (
        main(
            [
                "demo",
                "--config",
                str(config),
                "--width",
                "13",
                "--height",
                "7",
                "--output",
                str(image),
            ]
        )
        == 0
    )
    contents = image.read_bytes()
    header = b"P6\n13 7\n255\n"
    assert contents.startswith(header)
    assert len(contents) == len(header) + 13 * 7 * 3


def test_rejected_selection_preserves_existing_file(tmp_path, monkeypatch):
    monkeypatch.delenv("NPU_RENDER_BACKEND", raising=False)
    config = tmp_path / "config.json"
    save_backend("CPU", config)
    before = config.read_bytes()
    assert main(["select", "GPU.9999", "--config", str(config)]) == 1
    assert config.read_bytes() == before
