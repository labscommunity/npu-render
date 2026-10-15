import ctypes
import hashlib
import importlib.util
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    "build_ptl_kernel", Path(__file__).parents[1] / "tools/build_ptl_kernel.py"
)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def test_changed_toolchain_rejected_before_loading(tmp_path, monkeypatch):
    root = tmp_path / "bin"
    root.mkdir()
    expected = {}
    for name in module.TOOLS:
        value = name.encode()
        (root / name).write_bytes(value)
        expected[name] = hashlib.sha256(value).hexdigest()
    monkeypatch.setattr(module, "TOOLS", expected)
    assert module.checked_toolchain(tmp_path) == expected
    (root / "moviLLD64.dll").write_bytes(b"changed")
    with pytest.raises(ValueError, match="Untested toolchain DLL"):
        module.checked_toolchain(tmp_path)


@pytest.mark.parametrize("pointer,size", [(None, 1), (1, -1), (1, 16 * 1024 * 1024 + 1)])
def test_invalid_foreign_output_rejected_without_dereferencing(pointer, size):
    with pytest.raises(ValueError):
        module.output_bytes(pointer, size)


def test_copied_output_survives_foreign_buffer_changes():
    storage = ctypes.create_string_buffer(b"original")
    value = module.output_bytes(storage, 8)
    storage.value = b"changed"
    assert value == b"original"
    assert module.output_bytes(None, 0) == b""
