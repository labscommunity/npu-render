"""Exercise captured-window NPU presentation; does not prove DWM work retirement."""

import argparse
import ctypes
import json
import os
import subprocess
import tempfile
import time
from ctypes import wintypes as w
from pathlib import Path

from npu_render.windows_session import inspect_session, require_interactive_desktop


def run(args):
    if os.name != "nt":
        raise RuntimeError("Windows interactive desktop required")
    require_interactive_desktop(inspect_session())
    user = ctypes.WinDLL("user32", use_last_error=True)
    gdi = ctypes.WinDLL("gdi32", use_last_error=True)

    def bind(dll, name, result, *arguments):
        fn = getattr(dll, name)
        fn.restype, fn.argtypes = result, arguments
        return fn

    dpi = bind(user, "SetProcessDpiAwarenessContext", w.BOOL, ctypes.c_void_p)
    if not dpi(ctypes.c_void_p(-4)):
        raise ctypes.WinError(ctypes.get_last_error())

    find = bind(user, "FindWindowW", w.HWND, w.LPCWSTR, w.LPCWSTR)
    visible = bind(user, "IsWindowVisible", w.BOOL, w.HWND)
    exists = bind(user, "IsWindow", w.BOOL, w.HWND)
    get_pid = bind(user, "GetWindowThreadProcessId", w.DWORD, w.HWND, ctypes.POINTER(w.DWORD))
    foreground = bind(user, "GetForegroundWindow", w.HWND)
    class_name = bind(user, "GetClassNameW", ctypes.c_int, w.HWND, w.LPWSTR, ctypes.c_int)
    activate = bind(user, "SetForegroundWindow", w.BOOL, w.HWND)
    client_point = bind(user, "ClientToScreen", w.BOOL, w.HWND, ctypes.POINTER(w.POINT))
    move = bind(
        user,
        "SetWindowPos",
        w.BOOL,
        w.HWND,
        w.HWND,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        ctypes.c_int,
        w.UINT,
    )
    show = bind(user, "ShowWindow", w.BOOL, w.HWND, ctypes.c_int)
    send = bind(
        user,
        "SendMessageTimeoutW",
        ctypes.c_ssize_t,
        w.HWND,
        w.UINT,
        w.WPARAM,
        w.LPARAM,
        w.UINT,
        w.UINT,
        ctypes.POINTER(ctypes.c_size_t),
    )
    dc = bind(user, "GetDC", w.HDC, w.HWND)
    release = bind(user, "ReleaseDC", ctypes.c_int, w.HWND, w.HDC)
    pixel = bind(gdi, "GetPixel", w.DWORD, w.HDC, ctypes.c_int, ctypes.c_int)
    cursor = bind(user, "SetCursorPos", w.BOOL, ctypes.c_int, ctypes.c_int)
    get_cursor = bind(user, "GetCursorPos", w.BOOL, ctypes.POINTER(w.POINT))

    class Mouse(ctypes.Structure):
        _fields_ = [
            ("dx", w.LONG),
            ("dy", w.LONG),
            ("data", w.DWORD),
            ("flags", w.DWORD),
            ("time", w.DWORD),
            ("extra", ctypes.c_size_t),
        ]

    class Keyboard(ctypes.Structure):
        _fields_ = [
            ("vk", w.WORD),
            ("scan", w.WORD),
            ("flags", w.DWORD),
            ("time", w.DWORD),
            ("extra", ctypes.c_size_t),
        ]

    class Payload(ctypes.Union):
        _fields_ = [("mouse", Mouse), ("key", Keyboard)]

    class Input(ctypes.Structure):
        _fields_ = [("kind", w.DWORD), ("payload", Payload)]

    inputs = bind(user, "SendInput", w.UINT, w.UINT, ctypes.POINTER(Input), ctypes.c_int)
    old_cursor = w.POINT()
    get_cursor(ctypes.byref(old_cursor))
    old_foreground = foreground()
    report = {
        "scope": __doc__,
        "status": "running",
        "checks": [],
        "native": [],
        "session_before": inspect_session(),
    }
    fixture = child = None
    host = window = None
    logs = []
    args.report.parent.mkdir(parents=True, exist_ok=True)

    def message(hwnd, number, value=0):
        result = ctypes.c_size_t()
        if not send(hwnd, number, value, 0, 2, 2000, ctypes.byref(result)):
            raise RuntimeError("owned fixture message timed out")
        return result.value

    def wait(predicate, name, timeout=5):
        started = time.monotonic()
        while not predicate():
            if time.monotonic() - started >= timeout:
                raise RuntimeError("timed out: " + name)
            time.sleep(0.025)
        report["checks"].append({"name": name, "seconds": time.monotonic() - started})

    def colour(hwnd, location=(120, 100)):
        point = w.POINT(*location)
        if not client_point(hwnd, ctypes.byref(point)):
            raise RuntimeError("fixture client coordinate unavailable")
        return colour_at(point)

    def colour_at(point):
        surface = dc(None)
        if not surface:
            raise RuntimeError("screen DC unavailable")
        try:
            value = pixel(surface, point.x, point.y)
        finally:
            release(None, surface)
        if value == 0xFFFFFFFF:
            raise RuntimeError("screen pixel unavailable")
        return (value & 255, (value >> 8) & 255, (value >> 16) & 255)

    def emit(events):
        array = (Input * len(events))(*events)
        if inputs(len(events), array, ctypes.sizeof(Input)) != len(events):
            raise ctypes.WinError(ctypes.get_last_error())

    def key(vk, up=False):
        event = Input()
        event.kind, event.payload.key.vk, event.payload.key.flags = 1, vk, 2 if up else 0
        return event

    def hotkey(vk):
        keys = [0x11, 0x12, 0x10, vk]
        emit([key(k) for k in keys] + [key(k, True) for k in reversed(keys)])

    def launch(directory, name, backend=None):
        ready = directory / (name + ".ready")
        native = args.report.with_name(args.report.stem + "-" + name + ".json")
        state_path = directory / (name + ".state.json")
        log = args.report.with_name(args.report.stem + "-" + name + ".log").open("w")
        logs.append(log)
        command = [
            str(args.executable.resolve()),
            "--backend",
            backend or args.backend,
            "--seconds",
            "90",
            "--ready-file",
            str(ready),
            "--report",
            str(native),
            "--state-file",
            str(state_path),
            "--compare-original",
            "true",
        ]
        if args.calibrate_system_overlay:
            command += ["--calibrate-system-overlay", "true"]
        process = subprocess.Popen(
            command, stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW
        )
        return process, ready, native, state_path

    try:
        if find("NrCaptureFixture", None) or find("NpuRenderDesktopHost", None):
            raise RuntimeError("another owned desktop experiment is active")
        fixture = subprocess.Popen(
            [str(args.fixture.resolve()), "--fixture"], creationflags=subprocess.CREATE_NO_WINDOW
        )
        wait(lambda: find("NrCaptureFixture", None), "fixture launch")
        window = find("NrCaptureFixture", None)
        pid = w.DWORD()
        get_pid(window, ctypes.byref(pid))
        if pid.value != fixture.pid:
            raise RuntimeError("fixture ownership mismatch")
        message(window, 0x8001, 40)
        with tempfile.TemporaryDirectory(prefix="nr-desktop-check-") as temporary:
            directory = Path(temporary)
            child, ready, native, state_path = launch(directory, "functional")

            def ready_or_failed():
                if child.poll() is not None:
                    raise RuntimeError("desktop host exited before readiness")
                return ready.exists()

            wait(ready_or_failed, "NPU desktop readiness", 45)
            host = find("NpuRenderDesktopHost", None)
            get_pid(host, ctypes.byref(pid))
            if pid.value != child.pid:
                raise RuntimeError("desktop host ownership mismatch")
            wait(lambda: visible(host) and colour(window) == (22, 60, 78), "visible NPU fixture")
            point = w.POINT(120, 100)
            client_point(window, ctypes.byref(point))
            before = message(window, 0x8000)
            cursor(point.x, point.y)
            events = [Input(), Input()]
            events[0].payload.mouse.flags = 2
            events[1].payload.mouse.flags = 4
            emit(events)
            wait(lambda: message(window, 0x8000) == before + 1, "mouse reaches original window")
            activate(window)
            wait(lambda: foreground() == window, "original window owns keyboard focus")
            before = message(window, 0x8004)
            emit([key(0x58), key(0x58, True)])
            wait(lambda: message(window, 0x8004) == before + 1, "keyboard reaches original window")
            message(window, 0x8001, 90)
            wait(
                lambda: visible(host) and colour(window) == (22, 110, 78),
                "changed pixels through NPU",
            )
            if not move(window, None, 600, 250, 720, 490, 0x14):
                raise ctypes.WinError(ctypes.get_last_error())
            wait(
                lambda: visible(host) and colour(window) == (22, 110, 78),
                "move and resize through NPU",
            )
            popup = message(window, 0x8002)
            wait(
                lambda: visible(host) and colour(popup) == (231, 111, 23),
                "new overlapping popup through NPU",
            )
            message(window, 0x8002)
            wait(
                lambda: visible(host) and colour(window, (180, 260)) == (22, 110, 78),
                "popup closure restores underlying window",
            )
            for cycle in range(args.popup_cycles):
                popup = message(window, 0x8002)
                wait(
                    lambda popup=popup: visible(host) and colour(popup) == (231, 111, 23),
                    f"popup stress {cycle} shown",
                )
                message(window, 0x8002)
                wait(
                    lambda: visible(host) and colour(window, (180, 260)) == (22, 110, 78),
                    f"popup stress {cycle} removed",
                )
            for cycle in range(20):
                popup = message(window, 0x8006)
                wait(
                    lambda popup=popup: visible(host) and colour(popup) == (231, 111, 23),
                    f"hidden popup {cycle} shown",
                )
                message(window, 0x8006)
                wait(
                    lambda: visible(host) and colour(window, (180, 260)) == (22, 110, 78),
                    f"hidden popup {cycle} removed",
                )
            message(window, 0x8002)
            old_point = w.POINT(180, 260)
            client_point(window, ctypes.byref(old_point))
            show(window, 6)
            wait(
                lambda: visible(host) and colour_at(old_point) != (22, 110, 78),
                "minimize removes original surface",
            )
            show(window, 9)
            wait(
                lambda: visible(host) and colour(window) == (22, 110, 78),
                "restore recreates captured surface",
            )
            hotkey(0x7A)
            wait(lambda: not visible(host), "keyboard switch returns original desktop")
            wait(lambda: colour(window) == (22, 110, 78), "original desktop pixels after switch")

            def off_state():
                state = json.loads(state_path.read_text())
                return (
                    not state["enabled"]
                    and not state["sources"]
                    and not state["pending_retirements"]
                )

            wait(off_state, "disabled mode releases every capture")
            before_dispatches = json.loads(state_path.read_text())["dispatches"]
            message(window, 0x8001, 95)
            wait(
                lambda: colour(window) == (22, 115, 78), "original window changes while NPU is off"
            )
            time.sleep(0.8)
            after_dispatches = json.loads(state_path.read_text())["dispatches"]
            if before_dispatches != after_dispatches:
                raise RuntimeError("disabled mode still submitted NPU work")
            report["off_dispatches"] = after_dispatches - before_dispatches
            message(window, 0x8001, 90)
            hotkey(0x7A)
            wait(
                lambda: visible(host) and colour(window) == (22, 110, 78),
                "keyboard switch resumes NPU output",
            )
            hotkey(0x7B)
            wait(lambda: child.poll() is not None, "emergency exit")
            if child.returncode:
                raise RuntimeError("desktop host reported failure")
            report["native"].append(json.loads(native.read_text()))
            wait(
                lambda: not exists(host) and visible(window) and colour(window) == (22, 110, 78),
                "emergency exit restores originals",
            )
            child, ready, native, state_path = launch(directory, "crash")
            wait(ready_or_failed, "crash test readiness", 45)
            host = find("NpuRenderDesktopHost", None)
            child.kill()
            child.wait(timeout=5)
            wait(
                lambda: not exists(host) and visible(window) and colour(window) == (22, 110, 78),
                "process death restores originals",
            )
            child, _, native, state_path = launch(directory, "rejected", "INVALID")
            if child.wait(timeout=15) == 0:
                raise RuntimeError("unsupported backend unexpectedly accepted")
            report["native"].append(json.loads(native.read_text()))
            wait(
                lambda: not find("NpuRenderDesktopHost", None) and visible(window),
                "failed preparation preserves original desktop",
            )
        report["status"] = "passed"
    except Exception as error:
        report.update(status="failed", error=str(error))
        report["failure_state"] = {
            "host_exit": child.poll() if child else None,
            "host_exists": bool(host and exists(host)),
            "host_visible": bool(host and visible(host)),
            "fixture_exists": bool(window and exists(window)),
        }
        if window and exists(window):
            try:
                report["failure_state"]["fixture_screen_rgb"] = colour(window)
                name = ctypes.create_unicode_buffer(256)
                class_name(foreground(), name, 256)
                report["failure_state"]["foreground_class"] = name.value
                if host and exists(host) and visible(host):
                    message(host, 0x0312, 2)
                    time.sleep(0.2)
                    report["failure_state"]["original_fixture_rgb"] = colour(window)
                    activate(window)
                    time.sleep(0.2)
                    report["failure_state"]["activated_fixture_rgb"] = colour(window)
            except Exception as diagnostic_error:
                report["failure_state"]["pixel_error"] = str(diagnostic_error)
    finally:
        for process in (child, fixture):
            if process is not None and process.poll() is None:
                process.kill()
                process.wait(timeout=5)
        cursor(old_cursor.x, old_cursor.y)
        if old_foreground and exists(old_foreground):
            activate(old_foreground)
        for log in logs:
            log.close()
        report["session_after"] = inspect_session()
        args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(
        json.dumps(
            {
                "status": report["status"],
                "checks": len(report["checks"]),
                "error": report.get("error"),
            }
        )
    )
    return 0 if report["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--backend", choices=["CPU", "GPU", "NPU"], default="NPU")
    parser.add_argument("--calibrate-system-overlay", action="store_true")
    parser.add_argument("--popup-cycles", type=int, default=20)
    parser.add_argument("--report", type=Path, required=True)
    raise SystemExit(run(parser.parse_args()))


if __name__ == "__main__":
    main()
