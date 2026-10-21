import json
import re

import pytest

from npu_render import cli, desktop_switch

# A fictitious profile: the repository never carries the lab user's path.
ROOT = r"C:\Users\lab\AppData\Local\npu-render"
VM = ROOT + r"\vm"


def survey(completed=True, admission="native-desktop-admission-v93", sequence_done=True):
    return {
        "root": ROOT,
        "admissions": [
            "native-desktop-admission-v88-attempt1-screen-saver",
            "native-desktop-admission-v91",
            admission,
        ],
        "registrations": [
            {
                "name": "native-registration-v67",
                "passed": True,
                "package": "native-driver-package-v36",
            },
            {
                "name": "native-registration-v69",
                "passed": True,
                "package": "native-driver-package-v37",
            },
            {
                "name": "native-registration-v70",
                "passed": False,
                "package": "native-driver-package-v37",
            },
        ],
        "admission": {
            "name": admission,
            "stop_requested": False,
            "report": {
                "passed": completed,
                "completed": completed,
                "error": None,
                "started": "2026-10-21T19:02:11Z",
                "finished": "2026-10-21T21:05:40Z" if completed else None,
                "cleanup_passed": completed,
            },
            "bootstrap": {
                "passed": completed,
                "completed": completed,
                "error": None,
                "started": "2026-10-21T19:03:00Z",
                "finished": None,
                "observed_frame_count": 1234,
                "initial_grant_s": 2400,
                "lease_renewals": [
                    {
                        "observed": "2026-10-21T19:33:00Z",
                        "granted_s": 1800,
                        "deadlines": [100, 101],
                    },
                    {
                        "observed": "2026-10-21T20:03:00Z",
                        "granted_s": 1800,
                        "deadlines": [200, 201],
                    },
                ],
            },
            "recovery": {"passed": True},
        },
        "sequence": {
            "path": VM + r"\sequence-desktop-v93\status.json",
            "status": {
                "run_id": "sequence-desktop-v93-0a1b2c3d",
                "done": sequence_done,
                "passed": sequence_done,
                "steps": [
                    {"name": "stop-saver", "finished": "t", "ok": True},
                    {
                        "name": "ptl-native-live-v47",
                        "finished": "t",
                        "ok": False,
                        "error": "report false",
                    },
                ],
            },
        },
        "engine_summary": {
            "path": VM + r"\engine-samples-sequence-desktop-v93-summary.json",
            "summary": {
                "samples": 3600,
                "gpu_3d_percent_sum": {"p50": 4.2, "p90": 9.1, "max": 30.5},
            },
        },
    }


class FakeDelta:
    """Answers the module's remote reads from canned documents and records every script."""

    def __init__(self, monkeypatch, survey_doc, reads=None):
        self.scripts, self.uploads, self.reads = [], [], dict(reads or {})
        self.survey_doc = survey_doc
        monkeypatch.setattr(desktop_switch, "run_remote", self.run_remote)
        monkeypatch.setattr(desktop_switch, "upload", self.upload)

    def run_remote(self, script, timeout=120):
        self.scripts.append(script)
        if "GetFolderPath" in script and "admissions=" in script:
            return "#< CLIXML noise\n" + json.dumps(self.survey_doc)
        if "GetFolderPath" in script:
            return json.dumps(self.survey_doc)
        if "Register-ScheduledTask" in script:
            return "launched as T8-NpuRender-Sequence-x\n"
        match = re.match(r'\$p="([^"]+)";if\(Test-Path', script)
        if match:
            answers = self.reads.get(match.group(1), [])
            value = answers.pop(0) if len(answers) > 1 else (answers[0] if answers else None)
            return "" if value is None else json.dumps(value)
        return ""

    def upload(self, paths, remote_dir=desktop_switch.REMOTE_VM_SCP):
        for path in paths:
            self.uploads.append((path.name, remote_dir, path.read_text()))


def test_generation_counter_follows_the_generators_renames():
    names = survey()["admissions"]
    assert desktop_switch.next_generation(names) == 18
    assert desktop_switch.next_generation(["native-desktop-admission-v93-attempt1"]) == 18
    with pytest.raises(RuntimeError, match="no native-desktop-admission"):
        desktop_switch.next_generation(["native-registration-v69"])
    registrations = survey()["registrations"]
    assert (
        desktop_switch.last_passing_registration(registrations, "native-driver-package-v37") == 14
    )
    assert (
        desktop_switch.last_passing_registration(registrations, "native-driver-package-v36") == 12
    )
    with pytest.raises(RuntimeError, match="native-driver-package-v38"):
        desktop_switch.last_passing_registration(registrations, "native-driver-package-v38")
    names = desktop_switch.transaction_names(18)
    assert names == {
        "admission": "native-desktop-admission-v94",
        "topology": "native-topology-v172",
        "live": "ptl-native-live-v48",
        "sequence": "sequence-desktop-v94",
    }
    assert all(re.fullmatch(r"[a-z0-9-]+", name) for name in names.values())


def test_generator_command_carries_every_pin(tmp_path):
    config = desktop_switch.SwitchConfig()
    command = desktop_switch.generator_command(config, 18, 14, tmp_path)
    assert command[1].endswith("make_trial_chain.py")
    flags = dict(zip(command[2::2], command[3::2], strict=False))
    assert flags["--generation"] == "18" and flags["--registration-generation"] == "14"
    assert (
        flags["--build"] == "native-tree-v43" and flags["--package"] == "native-driver-package-v37"
    )
    assert (
        flags["--version"] == "0.0.1337.0" and flags["--frame-blobs"] == "frame-blobs-v18\\deploy"
    )
    assert (
        flags["--idd-package"] == "native-idd-package-v3" and flags["--idd-version"] == "0.0.1503.0"
    )
    assert flags["--monitor-seconds"] == "7200" and flags["--initial-grant-seconds"] == "2400"
    assert command[-2:] == ["--clean-stop", "--capture"]
    quiet = desktop_switch.SwitchConfig(capture=False)
    assert "--capture" not in desktop_switch.generator_command(quiet, 18, 14, tmp_path)


def test_enable_uploads_the_chain_and_launches_the_plan(monkeypatch):
    delta = FakeDelta(monkeypatch, survey())
    generated = {}

    def fake_generate(config, generation, registration, output):
        generated.update(generation=generation, registration=registration, config=config)
        output.mkdir(parents=True)
        files = {"run-native-desktop-v94.ps1": "a", "native-desktop-v94-source.json": "b"}
        for name in files:
            (output / name).write_text(name)
        return {"generation": generation, "files": files}

    monkeypatch.setattr(desktop_switch, "generate_chain", fake_generate)
    result = desktop_switch.enable(desktop_switch.SwitchConfig(monitor_seconds=7200))
    assert generated["generation"] == 18 and generated["registration"] == 14
    uploaded = {name: remote for name, remote, _ in delta.uploads}
    assert uploaded == {
        "run-native-desktop-v94.ps1": desktop_switch.REMOTE_VM_SCP,
        "native-desktop-v94-source.json": desktop_switch.REMOTE_VM_SCP,
        "sequence-desktop-v94.json": desktop_switch.REMOTE_VM_SCP,
    }
    plan = json.loads(delta.uploads[-1][2])
    assert plan["run_id"].startswith("sequence-desktop-v94-")
    assert plan["status"] == VM + r"\sequence-desktop-v94\status.json"
    assert plan["log_dir"] == VM + r"\sequence-desktop-v94\logs"
    steps = {step["name"]: step for step in plan["steps"]}
    assert list(steps) == [
        "stop-saver",
        "sampler",
        "native-topology-v172",
        "pause",
        "ptl-native-live-v48",
        "sampler-summary",
    ]
    assert steps["pause"]["script"] == VM + r"\pause.ps1" and steps["pause"]["named"] == {
        "Seconds": "8"
    }
    assert steps["stop-saver"]["script"] == VM + r"\stop_screen_saver.ps1"
    assert steps["stop-saver"]["ignore_exit"] is True
    assert steps["native-topology-v172"]["script"] == VM + r"\start-native-topology-v172.ps1"
    assert "report" not in steps["native-topology-v172"]
    samples = VM + r"\engine-samples-sequence-desktop-v94.jsonl"
    assert steps["sampler"]["named"] == {"Output": samples, "Seconds": "7500", "Interval": "2"}
    assert steps["ptl-native-live-v48"]["script"] == VM + r"\run-ptl-native-live-v48.ps1"
    assert steps["ptl-native-live-v48"]["report"] == VM + r"\ptl-native-live-v48\report.json"
    assert steps["ptl-native-live-v48"]["ignore_exit"] is True
    assert steps["sampler-summary"]["always"] is True
    assert (
        steps["sampler-summary"]["named"]["Output"] == samples[: -len(".jsonl")] + "-summary.json"
    )
    assert any(
        f'New-Item -ItemType Directory -Force "{VM}\\sequence-desktop-v94"' in s
        for s in delta.scripts
    )
    launch = [s for s in delta.scripts if "Register-ScheduledTask" in s]
    assert len(launch) == 1
    assert r'$p="$env:LOCALAPPDATA\npu-render\vm\sequence-desktop-v94.json"' in launch[0]
    assert "-LogonType S4U" in launch[0]
    assert result["generation"] == 18 and result["admission"] == "native-desktop-admission-v94"
    assert result["run_id"] == plan["run_id"] and result["status"] == plan["status"]
    assert result["launched"].startswith("launched as T8-NpuRender-Sequence-x")
    # Plan paths are literal, but only because the host reported its own root.
    assert all(s.count("C:\\Users") == s.count(ROOT) for s in delta.scripts)
    assert "C:\\Users" not in desktop_switch.SURVEY + desktop_switch.PREREQUISITES


def test_enable_refuses_while_an_admission_or_sequence_runs(monkeypatch):
    FakeDelta(monkeypatch, survey(completed=False))
    with pytest.raises(RuntimeError, match="native-desktop-admission-v93 has not completed"):
        desktop_switch.enable(desktop_switch.SwitchConfig())
    FakeDelta(monkeypatch, survey(sequence_done=False))
    with pytest.raises(RuntimeError, match="sequence-desktop-v93-0a1b2c3d is still running"):
        desktop_switch.enable(desktop_switch.SwitchConfig())


def test_disable_stops_waits_and_recovers(monkeypatch):
    transaction = VM + r"\native-desktop-admission-v93"
    status = VM + r"\sequence-recover-v93\status.json"
    reads = {
        transaction + r"\report.json": [
            {"completed": False, "passed": False},
            {"completed": True, "passed": True, "cleanup_passed": True, "error": None},
        ],
        transaction + r"\recovery-report.json": [{"passed": True, "devices_removed": 2}],
    }
    delta = FakeDelta(monkeypatch, survey(completed=False), reads)
    original = delta.run_remote

    def answer_status(script, timeout=120):
        if status in script and "Test-Path" in script:
            plan = json.loads(delta.uploads[-1][2])
            return json.dumps({"run_id": plan["run_id"], "done": True, "passed": True, "steps": []})
        return original(script, timeout)

    monkeypatch.setattr(desktop_switch, "run_remote", answer_status)
    result = desktop_switch.disable(interval=0, timeout=5)
    assert delta.scripts[1] == f'New-Item -ItemType File -Force "{transaction}\\stop"|Out-Null'
    plan = json.loads(delta.uploads[-1][2])
    assert plan["status"] == status and len(plan["steps"]) == 1
    step = plan["steps"][0]
    assert step["script"] == transaction + r"\recover.ps1"
    assert step["named"] == {"Root": transaction}
    assert step["report"] == transaction + r"\recovery-report.json"
    assert result["passed"] is True
    assert result["report"]["completed"] is True and result["recovery"]["devices_removed"] == 2
    with pytest.raises(RuntimeError, match="no running admission"):
        FakeDelta(monkeypatch, survey(completed=True))
        desktop_switch.disable(interval=0, timeout=1)


def test_status_summary_quotes_report_fields_without_a_verdict():
    text = desktop_switch.summarize(survey(completed=False))
    assert "admission native-desktop-admission-v93: not completed, stop requested: false" in text
    assert "report.json: passed=false completed=false error=null" in text
    assert "observed_frame_count=1234 initial_grant_s=2400 lease_renewals=2" in text
    assert (
        'last renewal: observed="2026-10-21T20:03:00Z" granted_s=1800 deadlines=[200, 201]' in text
    )
    assert "recovery-report.json: passed=true" in text
    assert "sequence sequence-desktop-v93-0a1b2c3d: done, passed=true, 2/2 steps finished" in text
    assert "ptl-native-live-v47: FAILED | report false" in text
    assert "engine samples engine-samples-sequence-desktop-v93-summary.json: samples=3600" in text
    assert "p50=4.2 p90=9.1 max=30.5" in text
    assert "npu" not in text.lower() and "offload" not in text.lower()
    assert desktop_switch.summarize({"admissions": []}) == "admission: none on the host"


def test_install_reports_missing_prerequisites(monkeypatch):
    found = {
        "root": ROOT,
        "build": {
            "path": ROOT + r"\builds\native-tree-v43",
            "exists": True,
            "report": {"clean_build": True},
        },
        "package": {
            "path": VM + r"\native-driver-package-v37",
            "exists": True,
            "report": {"prepared": True},
        },
        "indirect_display": {
            "path": VM + r"\native-idd-package-v3",
            "exists": True,
            "report": {"prepared": False},
        },
        "frame_blobs": {"path": VM + r"\frame-blobs-v18\deploy", "exists": False},
    }
    delta = FakeDelta(monkeypatch, found)
    result = desktop_switch.check_prerequisites(desktop_switch.SwitchConfig())
    assert result["ready"] is False and result["missing"] == ["indirect_display", "frame_blobs"]
    assert result["checks"]["package"] == {
        "path": VM + r"\native-driver-package-v37",
        "exists": True,
        "prepared": True,
        "ok": True,
    }
    script = delta.scripts[0]
    assert "[Environment]::GetFolderPath('LocalApplicationData')" in script
    assert r'$b="$root\builds\native-tree-v43"' in script and 'Test-Path "$b\\Release"' in script
    assert (
        r'$f="$vm\frame-blobs-v18\deploy"' in script and r'$i="$vm\native-idd-package-v3"' in script
    )
    assert "C:\\Users" not in script
    with pytest.raises(ValueError, match="package"):
        desktop_switch.check_prerequisites(desktop_switch.SwitchConfig(package="x; Remove-Item"))


def test_cli_desktop_subcommands(monkeypatch, capsys):
    FakeDelta(monkeypatch, survey())
    assert cli.main(["desktop", "status"]) == 0
    assert capsys.readouterr().out.startswith("admission native-desktop-admission-v93: completed")
    assert cli.main(["desktop", "status", "--json"]) == 0
    assert (
        json.loads(capsys.readouterr().out)["admission"]["bootstrap"]["observed_frame_count"]
        == 1234
    )
    monkeypatch.setattr(
        desktop_switch, "check_prerequisites", lambda config: {"ready": False, "missing": ["build"]}
    )
    assert cli.main(["desktop", "install", "--build", "native-tree-v44"]) == 1
    assert json.loads(capsys.readouterr().out)["missing"] == ["build"]
    seen = {}
    monkeypatch.setattr(
        desktop_switch, "enable", lambda config: seen.update(config=config) or {"run_id": "r"}
    )
    assert cli.main(["desktop", "enable", "--monitor-seconds", "600", "--no-capture"]) == 0
    assert seen["config"].monitor_seconds == 600 and seen["config"].capture is False
    assert seen["config"].package == "native-driver-package-v37"
    with pytest.raises(SystemExit):
        cli.main(["desktop", "--help"])
    help_text = capsys.readouterr().out
    assert all(word in help_text for word in ("status", "enable", "disable", "install"))


def test_prepare_packages_when_missing_and_registers(monkeypatch):
    delta = FakeDelta(monkeypatch, survey())
    generated = {}

    def fake_generate(config, generation, registration, output):
        generated.update(generation=generation, registration=registration)
        output.mkdir(parents=True)
        (output / "run-native-registration-v73.ps1").write_text("x")
        return {"generation": generation, "files": {"run-native-registration-v73.ps1": "x"}}

    monkeypatch.setattr(desktop_switch, "generate_chain", fake_generate)
    monkeypatch.setattr(
        desktop_switch,
        "check_prerequisites",
        lambda config: {
            "checks": {"package": {"ok": False}},
            "ready": False,
            "missing": ["package"],
        },
    )
    result = desktop_switch.prepare(desktop_switch.SwitchConfig())
    # Registration is the generation's own; the package script runs because the package is absent.
    assert generated == {"generation": 18, "registration": 18}
    assert result["registration"] == "native-registration-v73" and result["package_step"] is True
    plan = json.loads(delta.uploads[-1][2])
    steps = {step["name"]: step for step in plan["steps"]}
    assert list(steps) == ["package", "stop-saver", "registration-stage", "registration-render"]
    assert steps["package"]["report"] == VM + r"\native-driver-package-v37\report.json"
    assert steps["registration-stage"]["named"] == {"Mode": "stage"}
    assert (
        steps["registration-stage"]["report"] == VM + r"\native-registration-stage-v37\report.json"
    )
    assert steps["registration-render"]["named"] == {"Mode": "render"}
    assert steps["registration-render"]["report"] == VM + r"\native-registration-v73\report.json"
    assert plan["run_id"].startswith("sequence-prepare-v73-")
