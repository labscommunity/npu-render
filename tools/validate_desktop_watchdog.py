"""Pause only the owned desktop UI thread and verify external watchdog recovery."""

import argparse
import ctypes as c
import json
import os
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes as w
from pathlib import Path


def run(args):
    if os.name != "nt":
        raise RuntimeError("Windows required")
    user, kernel = (
        c.WinDLL("user32", use_last_error=True),
        c.WinDLL("kernel32", use_last_error=True),
    )
    user.FindWindowW.argtypes, user.FindWindowW.restype = [w.LPCWSTR, w.LPCWSTR], w.HWND
    user.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    user.GetWindowThreadProcessId.restype = w.DWORD
    kernel.OpenThread.argtypes, kernel.OpenThread.restype = [w.DWORD, w.BOOL, w.DWORD], w.HANDLE
    kernel.SuspendThread.argtypes, kernel.SuspendThread.restype = [w.HANDLE], w.DWORD
    kernel.ResumeThread.argtypes, kernel.ResumeThread.restype = [w.HANDLE], w.DWORD
    kernel.CloseHandle.argtypes = [w.HANDLE]
    if user.FindWindowW("NpuRenderDesktopHost", None):
        raise RuntimeError("another desktop host is active")
    result = {"scope": __doc__, "status": "running"}
    args.report.parent.mkdir(parents=True, exist_ok=True)
    thread = None
    wrapper = None
    try:
        with tempfile.TemporaryDirectory(prefix="nr-watchdog-check-") as directory:
            state = Path(directory) / "state.json"
            watchdog_report = args.report.with_name(args.report.stem + "-supervisor.json")
            command = [
                sys.executable,
                str(Path(__file__).with_name("run_desktop_capture.py")),
                "--executable",
                str(args.executable.resolve()),
                "--seconds",
                "30",
                "--stall-timeout",
                "2",
                "--state-path",
                str(state),
                "--report",
                str(watchdog_report),
            ]
            if args.calibrate_system_overlay:
                command += ["--calibrate-system-overlay"]
            wrapper = subprocess.Popen(command, creationflags=subprocess.CREATE_NO_WINDOW)
            deadline = time.monotonic() + 45
            while True:
                if wrapper.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("watchdog child never became ready")
                if state.exists():
                    snapshot = json.loads(state.read_text())
                    if snapshot["visible"] and snapshot["frames"]:
                        break
                time.sleep(0.05)
            hwnd = user.FindWindowW("NpuRenderDesktopHost", None)
            pid = w.DWORD()
            tid = user.GetWindowThreadProcessId(hwnd, c.byref(pid))
            if not tid or pid.value != snapshot["pid"]:
                raise RuntimeError("owned desktop window identity mismatch")
            # This fresh state path was passed only to our new supervisor. Match
            # its PID to the sole newly created output HWND before opening a thread.
            thread = kernel.OpenThread(0x0002, False, tid)
            if not thread:
                raise c.WinError(c.get_last_error())
            if kernel.SuspendThread(thread) != 0:
                raise RuntimeError("could not pause a previously running owned UI thread")
            started = time.monotonic()
            code = wrapper.wait(timeout=10)
            result["recovery_seconds"] = time.monotonic() - started
            result["supervisor"] = json.loads(watchdog_report.read_text())
            if code == 0 or not result["supervisor"]["watchdog_killed_child"]:
                raise RuntimeError("watchdog did not reject and terminate the stalled child")
            if user.FindWindowW("NpuRenderDesktopHost", None):
                raise RuntimeError("stalled desktop output remains visible")
            if result["recovery_seconds"] > 5:
                raise RuntimeError("watchdog recovery exceeded five seconds")
            result["status"] = "passed"
    except Exception as error:
        result.update(status="failed", error=str(error))
    finally:
        if thread:
            kernel.ResumeThread(thread)
            kernel.CloseHandle(thread)
        if wrapper and wrapper.poll() is None:
            # The supervisor retains its own 30-second deadline and owned-child
            # cleanup. Give it time to restore the original desktop after resume.
            wrapper.wait(timeout=40)
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(result))
    return 0 if result["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--calibrate-system-overlay", action="store_true")
    parser.add_argument("--report", type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()
