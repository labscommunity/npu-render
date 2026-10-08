"""Run the experimental captured-window desktop with an external hang watchdog."""

import argparse
import importlib.util
import json
import os
import subprocess
import tempfile
import time
from pathlib import Path

from npu_render.windows_session import inspect_session, require_interactive_desktop


def run(args):
    require_interactive_desktop(inspect_session())
    if os.name != "nt" or not args.executable.is_file():
        raise ValueError("requires Windows and nr_desktop_offload.exe")
    if not 1 <= args.seconds <= 3600 or not 2 <= args.stall_timeout <= 30:
        raise ValueError("duration must be 1–3600 seconds and hang timeout 2–30 seconds")
    package = importlib.util.find_spec("openvino")
    if package is None or package.origin is None:
        raise RuntimeError("the pinned OpenVINO runtime must be installed")
    environment = os.environ.copy()
    environment["PATH"] = (
        str(Path(package.origin).parent / "libs") + os.pathsep + environment["PATH"]
    )
    args.report.parent.mkdir(parents=True, exist_ok=True)
    result = {
        "scope": (
            "Captured-window composition; GPU capture/presentation and client rendering remain"
        ),
        "status": "running",
        "watchdog_killed_child": False,
    }
    print("Experimental desktop composition: GPU savings have not been demonstrated.", flush=True)
    print("Ctrl+Alt+Shift+F11 switches modes; Ctrl+Alt+Shift+F12 exits.", flush=True)
    with tempfile.TemporaryDirectory(prefix="nr-desktop-watchdog-") as directory:
        root = Path(directory)
        state, ready, stop = (root / name for name in ("state.json", "ready", "stop"))
        if args.state_path:
            state = args.state_path.resolve()
            state.parent.mkdir(parents=True, exist_ok=True)
            if state.exists():
                raise ValueError("external state path must be new for this launch")
        native_report = args.report.with_name(args.report.stem + "-native.json")
        command = [
            str(args.executable.resolve()),
            "--backend",
            args.backend,
            "--seconds",
            str(args.seconds),
            "--state-file",
            str(state),
            "--ready-file",
            str(ready),
            "--stop-file",
            str(stop),
            "--report",
            str(native_report),
        ]
        if args.calibrate_system_overlay:
            command += ["--calibrate-system-overlay", "true"]
        with args.report.with_suffix(".log").open("w", encoding="utf-8") as log:
            child = subprocess.Popen(
                command,
                env=environment,
                stdout=log,
                stderr=log,
                creationflags=subprocess.CREATE_NO_WINDOW,
            )
            result["child_pid"] = child.pid
            started = last_progress = time.monotonic()
            last_tick = None
            try:
                while child.poll() is None:
                    if state.exists():
                        snapshot = json.loads(state.read_text(encoding="utf-8"))
                        if snapshot["pid"] != child.pid:
                            raise RuntimeError("desktop state belongs to another process")
                        if snapshot["tick_ms"] != last_tick:
                            last_tick = snapshot["tick_ms"]
                            last_progress = time.monotonic()
                        result["last_state"] = snapshot
                    now = time.monotonic()
                    if last_tick is None and now - started > 45:
                        raise TimeoutError("desktop preparation timed out")
                    if last_tick is not None and now - last_progress > args.stall_timeout:
                        raise TimeoutError("desktop stopped responding; original desktop restored")
                    if now - started > args.seconds + 45:
                        raise TimeoutError("desktop exceeded its maximum lifetime")
                    time.sleep(0.1)
                if child.returncode:
                    raise RuntimeError(f"desktop exited with status {child.returncode}")
                result["native"] = json.loads(native_report.read_text(encoding="utf-8"))
                result["status"] = "passed"
            except KeyboardInterrupt:
                stop.write_text("stop\n", encoding="utf-8")
                result["status"] = "cancelled"
                try:
                    child.wait(timeout=2)
                except subprocess.TimeoutExpired:
                    pass
            except Exception as error:
                result.update(status="failed", error=str(error))
            finally:
                if child.poll() is None:
                    child.kill()
                    child.wait(timeout=5)
                    result["watchdog_killed_child"] = True
                result["elapsed_seconds"] = time.monotonic() - started
                args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": result["status"], "error": result.get("error")}), flush=True)
    return 0 if result["status"] in ("passed", "cancelled") else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--backend", choices=["CPU", "GPU", "NPU"], default="NPU")
    parser.add_argument("--seconds", type=int, default=3600)
    parser.add_argument("--stall-timeout", type=float, default=5)
    parser.add_argument("--calibrate-system-overlay", action="store_true")
    parser.add_argument("--state-path", type=Path)
    parser.add_argument("--report", type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()
