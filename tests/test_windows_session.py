from copy import deepcopy

import pytest

from npu_render.cli import main
from npu_render.windows_session import require_interactive_desktop


def active_session():
    return {
        "session_id": 1,
        "connection_state": 0,
        "window_station": {"name": "WinSta0", "error": 0},
        "thread_desktop": {"name": "Default", "error": 0},
        "input_desktop": {"name": "Default", "error": 0},
        "monitors": 1,
    }


@pytest.mark.parametrize(
    "changed",
    [
        {"session_id": 0},
        {"connection_state": 4},  # Disconnected sessions may still expose a Default desktop.
        {"input_desktop": {"name": "Screen-saver", "error": 0}},
        {"input_desktop": {"name": None, "error": 5}},
    ],
)
def test_unavailable_desktop_cannot_qualify(changed):
    with pytest.raises(RuntimeError):
        require_interactive_desktop({**active_session(), **changed})


def test_active_desktop_meets_preconditions_without_claiming_presentation():
    report = active_session()
    original = deepcopy(report)
    require_interactive_desktop(report)
    assert report == original


def test_cli_retains_screen_saver_evidence(monkeypatch, capsys):
    report = active_session()
    report["input_desktop"]["name"] = "Screen-saver"
    monkeypatch.setattr("npu_render.cli.inspect_session", lambda: report)
    assert main(["session", "--require-desktop"]) == 1
    output = capsys.readouterr().out
    assert '"passed": false' in output and '"Screen-saver"' in output
