import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace

import pytest

from npu_render.indirect_frame import confirmed_sole_display

_spec = importlib.util.spec_from_file_location(
    "collector", Path(__file__).parents[1] / "tools" / "collect_lab_frames.py"
)
collector = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(collector)


@pytest.mark.parametrize("ending", ["normal", "ssh-error", "invalid-frame", "interrupt"])
def test_collection_end_revokes_published_input_permission(tmp_path, monkeypatch, ending):
    output = tmp_path / "frames"
    gate = output / "input-ready.json"

    class Output(io.BytesIO):
        def readline(self, size=-1):
            if self.tell():
                # The collector really admitted input before the exit condition.
                assert confirmed_sole_display(gate)
                if ending == "interrupt":
                    raise KeyboardInterrupt
                if ending == "invalid-frame":
                    return b"NRFRAME:!\n"
            return super().readline(size)

    class Process:
        stdout = Output(b'NRSTATE:{"topology_name":"idd-topology-test","sole_indirect":true}\n')
        running = True

        def wait(self, timeout=None):
            self.running = False
            return 1 if ending == "ssh-error" else 0

        def poll(self):
            return None if self.running else 0

        def terminate(self):
            # Revocation must happen before a potentially slow process shutdown.
            assert not confirmed_sole_display(gate)
            self.running = False

    process = Process()
    monkeypatch.setattr(collector.subprocess, "Popen", lambda *a, **kw: process)
    args = SimpleNamespace(
        seconds=1,
        host="owned-lab",
        keep_frames=0,
        output=output,
        topology_name="idd-topology-test",
    )
    if ending == "normal":
        collector.run(args)
    else:
        error = {
            "ssh-error": RuntimeError,
            "invalid-frame": ValueError,
            "interrupt": KeyboardInterrupt,
        }[ending]
        with pytest.raises(error):
            collector.run(args)
    assert not confirmed_sole_display(gate)
    assert not process.running
