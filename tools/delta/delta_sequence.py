"""Launch a step sequence on Delta and follow its single status file.

The sequence itself runs on Delta under tools/delta/run_delta_sequence.ps1, detached
from the SSH session, and rewrites one JSON status document after every step. This
host-side helper uploads the plan, starts the runner, and polls that one document,
printing each step as it finishes. The run id in the plan is the only thing the
poller trusts, so a stale status file from an earlier sequence can never satisfy
it.

    uv run python tools/delta/delta_sequence.py launch plan.json
    uv run python tools/delta/delta_sequence.py wait plan.json [--interval 30]
    uv run python tools/delta/delta_sequence.py plan NAME --status 'C:\\...\\status.json' \\
        --log-dir 'C:\\...' --step name=SCRIPT[,arg,...][;match=REGEX][;report=PATH] ...

Plan files are plain JSON (see `plan`); paths inside them are Delta paths.
"""

import argparse
import base64
import json
import secrets
import subprocess
import sys
import time
from pathlib import Path

HOST = "delta"
RUNNER = r"$env:LOCALAPPDATA\npu-render\vm\run_delta_sequence.ps1"


def ssh_powershell(script: str, timeout: int = 120) -> str:
    encoded = base64.b64encode(script.encode("utf-16-le")).decode("ascii")
    result = subprocess.run(
        ["ssh", HOST, "powershell", "-NoProfile", "-EncodedCommand", encoded],
        capture_output=True,
        text=True,
        timeout=timeout,
    )
    return result.stdout


def upload(local: Path, remote_dir: str = "AppData/Local/npu-render/vm") -> str:
    subprocess.run(["scp", "-q", str(local), f"{HOST}:{remote_dir}/{local.name}"], check=True)
    return f"$env:LOCALAPPDATA\\npu-render\\vm\\{local.name}"


def launch(plan_path: Path) -> None:
    """Start the runner as a one-shot scheduled task so it outlives this SSH session.

    A process started with Start-Process from an SSH session dies with the session;
    a scheduled task under the same user (S4U, highest run level) runs detached and
    the runner unregisters it when the sequence ends.
    """
    plan = json.loads(plan_path.read_text())
    remote_plan = upload(plan_path)
    task = f"T8-NpuRender-Sequence-{plan['run_id']}"
    script = (
        f'$p="{remote_plan}";$r="{RUNNER}";$task="{task}";'
        "$action=New-ScheduledTaskAction -Execute "
        '"$env:windir\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" '
        "-Argument ('-NoProfile -NonInteractive -ExecutionPolicy Bypass -File \"'+$r+'\" "
        "-PlanPath \"'+$p+'\"');"
        "$principal=New-ScheduledTaskPrincipal -UserId "
        "([Security.Principal.WindowsIdentity]::GetCurrent().User.Value) "
        "-LogonType S4U -RunLevel Highest;"
        "$settings=New-ScheduledTaskSettingsSet -ExecutionTimeLimit (New-TimeSpan -Seconds 0) "
        "-AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -MultipleInstances IgnoreNew;"
        "Register-ScheduledTask -TaskName $task -Action $action -Principal $principal "
        "-Settings $settings -Force | Out-Null;"
        "Start-ScheduledTask -TaskName $task;"
        "[Console]::Out.WriteLine('launched as '+$task)"
    )
    print(ssh_powershell(script).strip(), plan["run_id"], plan["status"])


def read_status(status_path: str) -> dict | None:
    out = ssh_powershell(
        f'$p="{status_path}";if(Test-Path $p){{[Console]::Out.WriteLine((Get-Content $p -Raw))}}'
    )
    out = out.strip()
    if not out.startswith("{"):
        return None
    return json.loads(out)


def wait(plan_path: Path, interval: int) -> int:
    plan = json.loads(plan_path.read_text())
    reported = set()
    while True:
        status = read_status(plan["status"])
        if status and status.get("run_id") == plan["run_id"]:
            for step in status.get("steps", []):
                if step.get("finished") and step["name"] not in reported:
                    reported.add(step["name"])
                    verdict = "ok" if step.get("ok") else "FAILED"
                    line = f"{step['name']}: {verdict} in {step.get('seconds', '?')} s"
                    if step.get("matched"):
                        line += " | " + " ".join(step["matched"][:4])
                    if step.get("error"):
                        line += " | " + str(step["error"])[:200]
                    print(line, flush=True)
            if status.get("done"):
                print("sequence", "passed" if status.get("passed") else "FAILED", flush=True)
                return 0 if status.get("passed") else 1
        time.sleep(interval)


def make_plan(args: argparse.Namespace) -> None:
    steps = []
    for spec in args.step:
        fields = dict(part.split("=", 1) for part in spec.split(";"))
        script, *script_args = fields["name"].split(",")
        name = fields.get("label", Path(script).stem)
        step = {"name": name, "script": script, "args": []}
        # A PowerShell script's "-Name value" pairs become named parameters; everything else
        # stays positional (array splatting on Delta is positional).
        tokens = list(script_args)
        named = {}
        while tokens:
            token = tokens.pop(0)
            if script.lower().endswith(".ps1") and token.startswith("-") and tokens:
                named[token[1:]] = tokens.pop(0)
            else:
                step["args"].append(token)
        if named:
            step["named"] = named
        for key in ("match", "report"):
            if key in fields:
                step[key] = fields[key]
        for flag in ("continue", "always", "ignore_exit"):
            if fields.get(flag) == "1":
                step[flag] = True
        steps.append(step)
    plan = {
        "run_id": f"{args.name}-{secrets.token_hex(4)}",
        "status": args.status,
        "log_dir": args.log_dir,
        "steps": steps,
    }
    Path(args.output).write_text(json.dumps(plan, indent=2) + "\n")
    print(plan["run_id"], args.output)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("launch")
    p.add_argument("plan", type=Path)
    p = commands.add_parser("wait")
    p.add_argument("plan", type=Path)
    p.add_argument("--interval", type=int, default=30)
    p = commands.add_parser("plan")
    p.add_argument("name")
    p.add_argument("--status", required=True)
    p.add_argument("--log-dir", required=True)
    p.add_argument("--output", required=True)
    p.add_argument(
        "--step",
        action="append",
        required=True,
        help="name=SCRIPT[,ARG...][;label=L][;match=REGEX][;report=PATH][;continue=1][;always=1]",
    )
    args = parser.parse_args(argv)
    if args.command == "launch":
        launch(args.plan)
    elif args.command == "wait":
        return wait(args.plan, args.interval)
    else:
        make_plan(args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
