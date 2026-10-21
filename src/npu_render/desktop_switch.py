"""Host-side controller of the one user-facing desktop switch.

Every piece of an enable/disable cycle already runs on the Windows host as a numbered
research transaction emitted by tools/delta/make_trial_chain.py (see
docs/research/desktop-switch-design-2026-10-21.md). This module owns the generation
counter and drives those transactions from the development machine through two seams,
``run_remote`` (PowerShell over SSH) and ``upload`` (scp), so every decision is testable
without the host. The switch adds no verdict of its own: ``status`` quotes the fields the
transaction reports wrote, and only those scripts say whether anything was offloaded.

``install`` verifies prerequisites; with ``--prepare`` it runs the generated package script
when the package is missing and then the registration's stage and render qualification.
"""

import argparse
import base64
import contextlib
import functools
import importlib.util
import io
import json
import re
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, fields
from pathlib import Path

HOST = "delta"
# Expanded on the host; the repository never spells the user's profile path.
REMOTE_ROOT = r"$env:LOCALAPPDATA\npu-render"
REMOTE_VM_SCP = "AppData/Local/npu-render/vm"
ROOT = Path(__file__).resolve().parents[2]
GENERATOR = ROOT / "tools" / "delta" / "make_trial_chain.py"
SEQUENCE_HELPER = ROOT / "tools" / "delta" / "delta_sequence.py"
# The generator's renames: desktop admission 76 + g, registration 55 + g, readiness
# observer 136 + 2g, scoped live wrapper 30 + g.
ADMISSION_BASE, REGISTRATION_BASE, TOPOLOGY_BASE, LIVE_BASE = 76, 55, 136, 30
REPORT_KEYS = ("passed", "completed", "error", "started", "finished", "cleanup_passed")
BOOTSTRAP_KEYS = REPORT_KEYS[:5] + ("observed_frame_count", "initial_grant_s")
SAFE_NAME = re.compile(r"^[A-Za-z0-9_.\\-]+$")

# PowerShell continues a line after a pipe or inside braces, which keeps these readable.
PRELUDE = r"""$ErrorActionPreference='SilentlyContinue';$ProgressPreference='SilentlyContinue'
$root=[Environment]::GetFolderPath('LocalApplicationData')+'\npu-render';$vm="$root\vm"
function J($p){if(Test-Path $p){try{Get-Content $p -Raw|ConvertFrom-Json}catch{$null}}}
"""
# One round trip answers status, the generation counter and the running-admission checks.
SURVEY = (
    PRELUDE
    + r"""$dirs=@(Get-ChildItem $vm -Directory -Filter 'native-desktop-admission-v*')
$adm=@($dirs|Where-Object {$_.Name -match '^native-desktop-admission-v\d+$'}|
 Sort-Object {[int]($_.Name -replace '\D','')}|Select-Object -Last 1)
$seq=@(Get-Item "$vm\sequence-*\status.json"|Sort-Object LastWriteTimeUtc -Descending|
 Select-Object -First 1)
$sum=@(Get-Item "$vm\engine-samples-*-summary.json"|Sort-Object LastWriteTimeUtc -Descending|
 Select-Object -First 1)
$a=$null;$b=$null
if($adm){$t=$adm[0].FullName;$a=J "$t\report.json";$b=J "$t\bootstrap\report.json"}
$regs=@(Get-ChildItem $vm -Directory -Filter 'native-registration-v*'|
 Where-Object {$_.Name -match '^native-registration-v\d+$'}|ForEach-Object {
  $m=[regex]::Match([string](Get-Content "$vm\run-$($_.Name).ps1" -Raw),
   'native-driver-package-v\d+')
  [ordered]@{name=$_.Name;passed=[bool](J "$($_.FullName)\report.json").passed;
   package=$(if($m.Success){$m.Value})}})
$report=$(if($a){[ordered]@{passed=$a.passed;completed=$a.completed;error=$a.error;
 started=$a.started;finished=$a.finished;cleanup_passed=$a.cleanup_passed}})
$boot=$(if($b){[ordered]@{passed=$b.passed;completed=$b.completed;error=$b.error;
 started=$b.started;finished=$b.finished;observed_frame_count=$b.observed_frame_count;
 initial_grant_s=$b.initial_grant_s;
 lease_renewals=@($b.lease_renewals|Where-Object {$null -ne $_})}})
[ordered]@{root=$root;admissions=@($dirs|ForEach-Object Name);registrations=$regs;
 admission=$(if($adm){[ordered]@{name=$adm[0].Name;stop_requested=(Test-Path "$t\stop");
  report=$report;bootstrap=$boot;recovery=$(J "$t\recovery-report.json")}});
 sequence=$(if($seq){[ordered]@{path=$seq[0].FullName;status=(J $seq[0].FullName)}});
 engine_summary=$(if($sum){[ordered]@{path=$sum[0].FullName;summary=(J $sum[0].FullName)}})
}|ConvertTo-Json -Depth 7
"""
)
PREREQUISITES = (
    PRELUDE
    + r"""$b="$root\builds\@BUILD@";$p="$vm\@PACKAGE@";$i="$vm\@IDD@";$f="$vm\@BLOBS@"
[ordered]@{root=$root;
 build=[ordered]@{path=$b;exists=(Test-Path "$b\Release");report=$(J "$b\build-report.json")};
 package=[ordered]@{path=$p;exists=(Test-Path $p);report=$(J "$p\report.json")};
 indirect_display=[ordered]@{path=$i;exists=(Test-Path "$i\nr_native_indirect_probe.inf");
  report=$(J "$i\report.json")};
 frame_blobs=[ordered]@{path=$f;exists=(Test-Path "$f\manifest.txt")}
}|ConvertTo-Json -Depth 5
"""
)


@dataclass
class SwitchConfig:
    """Chain pins; the defaults are the latest qualified chain's (generation 28, soak v105)."""

    build: str = "native-tree-v45"
    package: str = "native-driver-package-v39"
    version: str = "0.0.1339.0"
    frame_blobs: str = "frame-blobs-v18\\deploy"
    idd_package: str = "native-idd-package-v3"
    idd_version: str = "0.0.1503.0"
    monitor_seconds: int = 7200
    initial_grant_seconds: int = 2400
    capture: bool = True
    # The measurement stimuli (owned input window, repeated-input benchmark, shared application)
    # belong to trials; a desktop switch renders the desktop as it is.
    stimulus: bool = False
    source: Path = ROOT / "results" / "render-driver"

    @classmethod
    def from_args(cls, args: argparse.Namespace) -> "SwitchConfig":
        return cls(**{f.name: getattr(args, f.name) for f in fields(cls) if hasattr(args, f.name)})


def run_remote(script: str, timeout: int = 120) -> str:
    """PowerShell over SSH; -EncodedCommand survives the quoting that -Command does not."""
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    command = ["ssh", HOST, "powershell", "-NoProfile", "-NonInteractive", "-EncodedCommand"]
    result = subprocess.run([*command, encoded], capture_output=True, text=True, timeout=timeout)
    if result.returncode and not result.stdout.strip():
        raise RuntimeError(f"ssh {HOST} failed ({result.returncode}): {result.stderr[-400:]}")
    return result.stdout


def upload(paths: list[Path], remote_dir: str = REMOTE_VM_SCP) -> None:
    subprocess.run(["scp", "-q", *map(str, paths), f"{HOST}:{remote_dir}/"], check=True)


@functools.cache
def sequence_helper():
    """tools/delta/delta_sequence.py owns the plan grammar and the detached launch; its SSH
    and scp calls are routed through this module's seams so one fake covers both."""
    spec = importlib.util.spec_from_file_location("delta_sequence", SEQUENCE_HELPER)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    module.ssh_powershell = lambda script, timeout=120: run_remote(script, timeout)

    def upload_one(local: Path, remote_dir: str = REMOTE_VM_SCP) -> str:
        upload([local], remote_dir)
        return f"{REMOTE_ROOT}\\vm\\{local.name}"

    module.upload = upload_one
    return module


def parse_json(text: str) -> dict | None:
    start = text.find("{")
    return None if start < 0 else json.JSONDecoder().raw_decode(text[start:])[0]


def remote_json(script: str, timeout: int = 120) -> dict:
    out = run_remote(script, timeout)
    document = parse_json(out)
    if document is None:
        raise RuntimeError("Delta returned no JSON: " + out.strip()[-300:])
    return document


def read_remote_json(path: str) -> dict | None:
    script = f'$p="{path}";if(Test-Path $p){{[Console]::Out.WriteLine((Get-Content $p -Raw))}}'
    return parse_json(run_remote(script))


def survey_host() -> dict:
    return remote_json(SURVEY, timeout=300)


def fill(template: str, **values: str) -> str:
    for key, value in values.items():
        if not SAFE_NAME.match(value):
            raise ValueError(f"{key.lower()} must name a host directory, not {value!r}")
        template = template.replace(f"@{key}@", value)
    return template


def numbered(prefix: str, names) -> list[int]:
    return [int(m.group(1)) for n in names if (m := re.match(prefix + r"-v(\d+)", n))]


def next_generation(admissions: list[str]) -> int:
    """Renamed failed attempts keep their number, so the counter passes them too."""
    present = numbered("native-desktop-admission", admissions)
    if not present:
        raise RuntimeError("no native-desktop-admission-v<N> on the host to continue counting from")
    return max(present) - ADMISSION_BASE + 1


def last_passing_registration(registrations: list[dict], package: str) -> int:
    """A registration qualifies one package: the runner script names it, the report does not."""
    passing = [r["name"] for r in registrations if r.get("passed") and r.get("package") == package]
    if not passing:
        raise RuntimeError(f"no passing native-registration-v<N> for {package}; register it first")
    return max(numbered("native-registration", passing)) - REGISTRATION_BASE


def transaction_names(generation: int) -> dict[str, str]:
    return {
        "admission": f"native-desktop-admission-v{ADMISSION_BASE + generation}",
        "topology": f"native-topology-v{TOPOLOGY_BASE + 2 * generation}",
        "live": f"ptl-native-live-v{LIVE_BASE + generation}",
        "sequence": f"sequence-desktop-v{ADMISSION_BASE + generation}",
    }


def generator_command(config: SwitchConfig, generation: int, registration: int, output: Path):
    command = [sys.executable, str(GENERATOR), "--source", str(config.source)]
    command += ["--output", str(output), "--build", config.build, "--package", config.package]
    command += ["--version", config.version, "--frame-blobs", config.frame_blobs]
    command += ["--generation", str(generation), "--registration-generation", str(registration)]
    command += ["--monitor-seconds", str(config.monitor_seconds)]
    command += ["--initial-grant-seconds", str(config.initial_grant_seconds)]
    command += ["--idd-package", config.idd_package, "--idd-version", config.idd_version]
    # The switch's admissions end on `disable`'s stop file, which the generated bootstrap then
    # treats as a clean end rather than a trial interruption.
    command.append("--clean-stop")
    if not config.stimulus:
        command.append("--no-stimulus")
    return command + (["--capture"] if config.capture else [])


def generate_chain(config: SwitchConfig, generation: int, registration: int, output: Path) -> dict:
    """The manifest lists every file the admission's source check will hash on the host."""
    command = generator_command(config, generation, registration, output)
    result = subprocess.run(command, capture_output=True, text=True)
    if result.returncode:
        raise RuntimeError("chain generator failed: " + result.stderr.strip()[-600:])
    return json.loads((output / "chain-manifest.json").read_text())


def plan_steps(root: str, generation: int, config: SwitchConfig) -> list[str]:
    """The generation-17 plan plus the readiness observer, in delta_sequence's step grammar.

    The observer writes its report.json only when it ends, so its start step relies on the
    start script's own throw rather than a report check that would fail while it is healthy.
    The live wrapper starts the admission (-Mode admit) and keeps running beside it.
    """
    vm, names = f"{root}\\vm", transaction_names(generation)
    samples = f"{vm}\\engine-samples-{names['sequence']}.jsonl"
    summary = f"{vm}\\engine-samples-{names['sequence']}-summary.json"
    # The readiness observer is a short pre-check, so the wrapper follows it directly; the pause
    # lets the observer's first inspection finish before the wrapper's own process checks.
    return [
        f"name={vm}\\stop_screen_saver.ps1;label=stop-saver;ignore_exit=1",
        f"name={vm}\\start_engine_sampler.ps1,-Output,{samples},-Seconds,"
        f"{config.monitor_seconds + 300},-Interval,2;label=sampler",
        f"name={vm}\\start-{names['topology']}.ps1;label={names['topology']}",
        f"name={vm}\\pause.ps1,-Seconds,8;label=pause",
        f"name={vm}\\run-{names['live']}.ps1;label={names['live']}"
        f";report={vm}\\{names['live']}\\report.json;ignore_exit=1",
        f"name={vm}\\summarize_engine_samples.ps1,-Samples,{samples},-Output,{summary}"
        ";label=sampler-summary;always=1",
    ]


def launch(name: str, root: str, steps: list[str], scratch: Path) -> dict:
    """Write the plan through delta_sequence, create its status directory and start it."""
    status_dir, plan_path = f"{root}\\vm\\{name}", scratch / f"{name}.json"
    status, log_dir = f"{status_dir}\\status.json", f"{status_dir}\\logs"
    spec = argparse.Namespace(name=name, status=status, log_dir=log_dir, output=str(plan_path))
    spec.step = steps
    with contextlib.redirect_stdout(io.StringIO()):
        sequence_helper().make_plan(spec)
    plan = json.loads(plan_path.read_text())
    # The runner writes its first status before it creates any directory.
    run_remote(f'New-Item -ItemType Directory -Force "{status_dir}"|Out-Null')
    with contextlib.redirect_stdout(io.StringIO()) as out:
        sequence_helper().launch(plan_path)
    return {"run_id": plan["run_id"], "status": status, "launched": out.getvalue().strip()}


def poll(read, ready, interval: float, timeout: float, what: str):
    deadline = time.monotonic() + timeout
    while True:
        value = read()
        if ready(value):
            return value
        if time.monotonic() >= deadline:
            raise RuntimeError(f"{what} within {timeout} s")
        time.sleep(interval)


def enable(config: SwitchConfig) -> dict:
    """Generate the next chain, upload it and launch its sequence; status follows it."""
    survey = survey_host()
    admission = survey.get("admission") or {}
    if admission and not (admission.get("report") or {}).get("completed"):
        raise RuntimeError(f"{admission['name']} has not completed; run `desktop disable` first")
    sequence = (survey.get("sequence") or {}).get("status") or {}
    if sequence and not sequence.get("done"):
        raise RuntimeError(f"sequence {sequence.get('run_id')} is still running")
    generation = next_generation(survey["admissions"])
    registration = last_passing_registration(survey["registrations"], config.package)
    names = transaction_names(generation)
    with tempfile.TemporaryDirectory() as tmp:
        chain = Path(tmp) / "chain"
        manifest = generate_chain(config, generation, registration, chain)
        upload([chain / name for name in manifest["files"]], REMOTE_VM_SCP)
        steps = plan_steps(survey["root"], generation, config)
        launched = launch(names["sequence"], survey["root"], steps, Path(tmp))
    return {
        "generation": generation,
        "registration_generation": registration,
        "admission": names["admission"],
        "uploaded": sorted(manifest["files"]),
        **launched,
        "next": "`npu-render desktop status` quotes the admission's reports as they appear",
    }


def disable(interval: float = 15, timeout: float = 900) -> dict:
    """Ask the bootstrap to restore, wait for the admission to complete, then recover again."""
    survey = survey_host()
    admission = survey.get("admission")
    if not admission or (admission.get("report") or {}).get("completed"):
        raise RuntimeError("no running admission on the host")
    transaction = f"{survey['root']}\\vm\\{admission['name']}"
    run_remote(f'New-Item -ItemType File -Force "{transaction}\\stop"|Out-Null')
    report = poll(
        lambda: read_remote_json(f"{transaction}\\report.json"),
        lambda r: bool(r and r.get("completed")),
        interval,
        timeout,
        f"{admission['name']} did not complete after its stop file",
    )
    step = f"name={transaction}\\recover.ps1,-Root,{transaction};label=recover"
    step += f";report={transaction}\\recovery-report.json"
    name = admission["name"].replace("native-desktop-admission", "sequence-recover")
    with tempfile.TemporaryDirectory() as tmp:
        launched = launch(name, survey["root"], [step], Path(tmp))
    sequence = poll(
        lambda: read_remote_json(launched["status"]),
        lambda s: bool(s and s.get("run_id") == launched["run_id"] and s.get("done")),
        interval,
        timeout,
        "recovery sequence did not finish",
    )
    recovery = read_remote_json(f"{transaction}\\recovery-report.json") or {}
    return {
        "admission": admission["name"],
        "report": {key: report.get(key) for key in REPORT_KEYS},
        "recovery_sequence": sequence,
        "recovery": recovery,
        "passed": bool(recovery.get("passed")) and bool(sequence.get("passed")),
    }


def prepare(config: SwitchConfig) -> dict:
    """Package the build if the package is missing, then register and qualify it.

    Registration is its own numbered transaction (the generator's registration family), so
    prepare generates the next chain with that generation as its own registration, uploads it,
    and runs the package script when needed, then the registration's stage and render modes.
    The admission is not started; `enable` finds the passing registration afterwards.
    """
    survey = survey_host()
    sequence = (survey.get("sequence") or {}).get("status") or {}
    if sequence and not sequence.get("done"):
        raise RuntimeError(f"sequence {sequence.get('run_id')} is still running")
    generation = next_generation(survey["admissions"])
    checks = check_prerequisites(config)
    vm = f"{survey['root']}\\vm"
    registration = f"native-registration-v{REGISTRATION_BASE + generation}"
    stage = config.package.replace("native-driver-package-", "native-registration-stage-")
    with tempfile.TemporaryDirectory() as tmp:
        chain = Path(tmp) / "chain"
        manifest = generate_chain(config, generation, generation, chain)
        upload([chain / name for name in manifest["files"]], REMOTE_VM_SCP)
        steps = []
        if not checks["checks"]["package"]["ok"]:
            steps.append(
                f"name={vm}\\package-native-driver-v10.ps1;label=package"
                f";report={vm}\\{config.package}\\report.json"
            )
        steps += [
            f"name={vm}\\stop_screen_saver.ps1;label=stop-saver;ignore_exit=1",
            f"name={vm}\\run-{registration}.ps1,-Mode,stage;label=registration-stage"
            f";report={vm}\\{stage}\\report.json;ignore_exit=1",
            f"name={vm}\\run-{registration}.ps1,-Mode,render;label=registration-render"
            f";report={vm}\\{registration}\\report.json;ignore_exit=1",
        ]
        launched = launch(
            f"sequence-prepare-v{REGISTRATION_BASE + generation}", survey["root"], steps, Path(tmp)
        )
    return {
        "generation": generation,
        "registration": registration,
        "package_step": not checks["checks"]["package"]["ok"],
        "uploaded": sorted(manifest["files"]),
        **launched,
        "next": "`npu-render desktop status` shows the sequence; `enable` once it passed",
    }


def check_prerequisites(config: SwitchConfig) -> dict:
    names = dict(BUILD=config.build, PACKAGE=config.package, IDD=config.idd_package)
    found = remote_json(fill(PREREQUISITES, BLOBS=config.frame_blobs, **names))
    verdicts = {"build": "clean_build", "package": "prepared", "indirect_display": "prepared"}
    checks = {}
    for name in ("build", "package", "indirect_display", "frame_blobs"):
        item, key = found[name], verdicts.get(name)
        report = item.get("report") or {}
        check = {"path": item["path"], "exists": bool(item.get("exists"))}
        if key:
            check[key] = report.get(key)
        check["ok"] = check["exists"] and (key is None or bool(report.get(key)))
        checks[name] = check
    missing = [name for name, check in checks.items() if not check["ok"]]
    return {
        "ready": not missing,
        "missing": missing,
        "checks": checks,
        "packaging": "`install --prepare` runs the package script and the registration",
    }


def quoted(document: dict, keys) -> str:
    """Render fields exactly as the report wrote them (true/false/null), never reworded."""
    return " ".join(f"{k}={json.dumps(document.get(k))}" for k in keys)


def summarize(survey: dict) -> str:
    """Quote the reports' own fields; the switch adds no verdict of its own."""
    lines = []
    admission = survey.get("admission")
    if not admission:
        lines.append("admission: none on the host")
    else:
        report, boot = admission.get("report") or {}, admission.get("bootstrap") or {}
        state = "completed" if report.get("completed") else "not completed"
        lines.append(f"admission {admission['name']}: {state}, ")
        lines[-1] += "stop requested: " + json.dumps(admission.get("stop_requested"))
        lines.append("  report.json: " + quoted(report, REPORT_KEYS))
        renewals = boot.get("lease_renewals") or []
        lines.append("  bootstrap/report.json: " + quoted(boot, BOOTSTRAP_KEYS))
        lines[-1] += f" lease_renewals={len(renewals)}"
        if renewals:
            last_keys = ("observed", "granted_s", "deadlines")
            lines.append("    last renewal: " + quoted(renewals[-1], last_keys))
        if admission.get("recovery"):
            lines.append("  recovery-report.json: " + quoted(admission["recovery"], ("passed",)))
    status = (survey.get("sequence") or {}).get("status")
    if status:
        steps = status.get("steps") or []
        done = "done" if status.get("done") else "running"
        finished = sum(1 for s in steps if s.get("finished"))
        lines.append(f"sequence {status.get('run_id')}: {done}, " + quoted(status, ("passed",)))
        lines[-1] += f", {finished}/{len(steps)} steps finished"
        for s in steps:
            verdict = ("ok" if s.get("ok") else "FAILED") if s.get("finished") else "running"
            error = f" | {str(s['error'])[:120]}" if s.get("error") else ""
            lines.append(f"  {s.get('name')}: {verdict}{error}")
    summary = survey.get("engine_summary") or {}
    if summary.get("summary"):
        gpu = summary["summary"].get("gpu_3d_percent_sum") or {}
        leaf = summary["path"].rsplit("\\", 1)[-1]
        lines.append(f"engine samples {leaf}: " + quoted(summary["summary"], ("samples",)))
        lines[-1] += " gpu_3d_percent_sum " + quoted(gpu, ("p50", "p90", "max"))
    return "\n".join(lines)


def add_config_arguments(parser: argparse.ArgumentParser, full: bool) -> None:
    d = SwitchConfig()
    options = [
        ("--build", d.build, "builds\\<name> holding the UMD and worker"),
        ("--package", d.package, "vm\\<name> signed driver package"),
        ("--idd-package", d.idd_package, "vm\\<name> indirect display package"),
        ("--frame-blobs", d.frame_blobs, "vm\\<path> frame kernel blobs"),
    ]
    if full:
        options += [
            ("--version", d.version, "driver package version"),
            ("--idd-version", d.idd_version, "indirect display package version"),
            ("--monitor-seconds", d.monitor_seconds, "admission length before the stop file"),
            ("--initial-grant-seconds", d.initial_grant_seconds, "first lease length"),
            ("--source", d.source, "fetched base chain the generator derives from"),
        ]
        parser.add_argument("--no-capture", dest="capture", action="store_false")
        parser.add_argument(
            "--stimulus",
            action="store_true",
            help="run the trial measurement stimuli beside the desktop (trial mode)",
        )
    for flag, default, help_text in options:
        parser.add_argument(flag, default=default, type=type(default), help=help_text)


def add_parser(commands) -> None:
    desktop = commands.add_parser("desktop", help="control the desktop switch on the Windows host")
    sub = desktop.add_subparsers(dest="desktop_command", required=True)
    status = sub.add_parser(
        "status", help="quote the newest admission, sequence and sample reports"
    )
    status.add_argument("--json", action="store_true", help="print the survey instead of a summary")
    enable_ = sub.add_parser("enable", help="generate, upload and launch the next admission")
    add_config_arguments(enable_, full=True)
    disable_ = sub.add_parser("disable", help="stop the running admission and run its recovery")
    disable_.add_argument("--interval", type=float, default=15, help="poll interval in seconds")
    disable_.add_argument("--timeout", type=float, default=900, help="seconds to wait per phase")
    install = sub.add_parser(
        "install", help="verify package, display, build and blobs are prepared"
    )
    add_config_arguments(install, full=True)
    install.add_argument(
        "--prepare",
        action="store_true",
        help="package the build if needed, then run and qualify its registration",
    )


def run(args: argparse.Namespace) -> dict:
    if args.desktop_command == "status":
        return survey_host()
    if args.desktop_command == "enable":
        return enable(SwitchConfig.from_args(args))
    if args.desktop_command == "disable":
        return disable(args.interval, args.timeout)
    if getattr(args, "prepare", False):
        return prepare(SwitchConfig.from_args(args))
    return check_prerequisites(SwitchConfig.from_args(args))
