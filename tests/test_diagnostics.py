from types import SimpleNamespace

import pytest

from npu_render.cli import main
from npu_render.openvino_diagnostics import inspect_stack, require_npu_driver


class PendingDriver:
    # Reproduces an installed new INF while the runtime still uses the old driver.
    available_devices = ["NPU"]
    installed_package_version = "32.0.100.5540"

    def get_property(self, device, key):
        if key == "SUPPORTED_PROPERTIES":
            return {"FULL_DEVICE_NAME": "RO", "NPU_DRIVER_VERSION": "RO"}
        return {"FULL_DEVICE_NAME": "Intel NPU", "NPU_DRIVER_VERSION": "1004724"}[key]

    def get_versions(self, device):
        return {"NPU": SimpleNamespace(build_number="fixture", description="fixture plugin")}


def test_installed_package_cannot_satisfy_loaded_driver_gate():
    report = inspect_stack(PendingDriver())
    with pytest.raises(RuntimeError, match="runtime reported.*1004724"):
        require_npu_driver(report, "1005540")
    require_npu_driver(report, "1004724")
    assert "NPU_COMPILER_VERSION" in report["devices"][0]["errors"]


@pytest.mark.parametrize("devices", [[], [{"id": "NPU", "properties": {}}]])
def test_missing_version_cannot_pass(devices):
    with pytest.raises(RuntimeError):
        require_npu_driver({"devices": devices}, "1005540")


def test_cli_preserves_failed_stack_report(monkeypatch, capsys):
    monkeypatch.setattr("npu_render.cli.inspect_stack", lambda: inspect_stack(PendingDriver()))
    assert main(["stack", "--expected-npu-driver", "1005540"]) == 1
    output = capsys.readouterr().out
    assert '"passed": false' in output and '"1004724"' in output
