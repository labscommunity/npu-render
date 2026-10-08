import hashlib
import importlib.util
import json
import zipfile
from pathlib import Path

import pytest

spec = importlib.util.spec_from_file_location(
    "prepare_wdk", Path(__file__).parents[1] / "tools/prepare_wdk.py"
)
wdk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wdk)


def package(tmp_path, entries):
    archive = tmp_path / "wdk.nupkg"
    with zipfile.ZipFile(archive, "w") as output:
        for name, data in entries.items():
            output.writestr(wdk.PREFIX + name, data)
    return hashlib.sha256(archive.read_bytes()).hexdigest()


def test_changed_package_rejected_before_extraction(tmp_path):
    package(tmp_path, {"um/d3d10umddi.h": b"tampered"})
    with pytest.raises(RuntimeError, match="checksum"):
        wdk.prepare(tmp_path)
    assert not (tmp_path / "include").exists()


def test_header_path_cannot_escape_dependency(tmp_path, monkeypatch):
    digest = package(tmp_path, {"../../escaped.h": b"outside"})
    monkeypatch.setattr(wdk, "SHA256", digest)
    with pytest.raises(RuntimeError, match="unsafe"):
        wdk.prepare(tmp_path)
    assert not (tmp_path.parent / "escaped.h").exists()


def test_verified_headers_have_reproducible_manifest(tmp_path, monkeypatch):
    data = b"fixture header"
    digest = package(tmp_path, {"um/d3d10umddi.h": data})
    monkeypatch.setattr(wdk, "SHA256", digest)
    include = wdk.prepare(tmp_path)
    assert (include / "um/d3d10umddi.h").read_bytes() == data
    manifest = json.loads((tmp_path / "manifest.json").read_text())
    assert manifest["sha256"] == digest
    assert manifest["headers_sha256"] == {"um/d3d10umddi.h": hashlib.sha256(data).hexdigest()}
