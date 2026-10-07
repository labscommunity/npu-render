"""Read-only Windows desktop preflight; it never unlocks or switches a desktop."""

import ctypes as c
import os
from ctypes import wintypes as w


def inspect_session() -> dict:
    if os.name != "nt":
        raise RuntimeError("desktop inspection requires Windows")
    user = c.WinDLL("user32", use_last_error=True)
    kernel = c.WinDLL("kernel32", use_last_error=True)
    wts = c.WinDLL("wtsapi32", use_last_error=True)
    kernel.GetCurrentThreadId.argtypes, kernel.GetCurrentThreadId.restype = [], w.DWORD
    kernel.ProcessIdToSessionId.argtypes = [w.DWORD, c.POINTER(w.DWORD)]
    kernel.ProcessIdToSessionId.restype = w.BOOL
    user.GetProcessWindowStation.argtypes, user.GetProcessWindowStation.restype = [], w.HANDLE
    user.GetThreadDesktop.argtypes, user.GetThreadDesktop.restype = [w.DWORD], w.HANDLE
    user.OpenInputDesktop.argtypes = [w.DWORD, w.BOOL, w.DWORD]
    user.OpenInputDesktop.restype = w.HANDLE
    user.CloseDesktop.argtypes, user.CloseDesktop.restype = [w.HANDLE], w.BOOL
    user.GetUserObjectInformationW.argtypes = [
        w.HANDLE,
        c.c_int,
        w.LPVOID,
        w.DWORD,
        c.POINTER(w.DWORD),
    ]
    user.GetUserObjectInformationW.restype = w.BOOL
    user.GetSystemMetrics.argtypes, user.GetSystemMetrics.restype = [c.c_int], c.c_int
    wts.WTSQuerySessionInformationW.argtypes = [
        w.HANDLE,
        w.DWORD,
        c.c_int,
        c.POINTER(c.c_void_p),
        c.POINTER(w.DWORD),
    ]
    wts.WTSQuerySessionInformationW.restype = w.BOOL
    wts.WTSFreeMemory.argtypes, wts.WTSFreeMemory.restype = [c.c_void_p], None

    def object_name(handle):
        if not handle:
            return {"name": None, "error": c.get_last_error()}
        # UOI_NAME returns a byte count including the UTF-16 terminator.
        needed = w.DWORD()
        user.GetUserObjectInformationW(handle, 2, None, 0, c.byref(needed))
        if not 0 < needed.value <= 65536:
            return {"name": None, "error": c.get_last_error()}
        buffer = c.create_unicode_buffer(
            (needed.value + c.sizeof(w.WCHAR) - 1) // c.sizeof(w.WCHAR)
        )
        if not user.GetUserObjectInformationW(handle, 2, buffer, c.sizeof(buffer), c.byref(needed)):
            return {"name": None, "error": c.get_last_error()}
        return {"name": buffer.value, "error": 0}

    report = {
        "session_id": None,
        "connection_state": None,
        "errors": {},
        "scope": "Desktop preconditions only; presentation and scanout remain separate checks.",
    }
    session = w.DWORD()
    if kernel.ProcessIdToSessionId(os.getpid(), c.byref(session)):
        report["session_id"] = session.value
        buffer, size = c.c_void_p(), w.DWORD()
        # WTSConnectState (8) returns WTS_CONNECTSTATE_CLASS; WTSActive is zero.
        if wts.WTSQuerySessionInformationW(None, session.value, 8, c.byref(buffer), c.byref(size)):
            try:
                if buffer and size.value >= c.sizeof(c.c_int):
                    report["connection_state"] = c.cast(buffer, c.POINTER(c.c_int)).contents.value
                else:
                    report["errors"]["connection_state"] = "missing connection state"
            finally:
                if buffer:
                    wts.WTSFreeMemory(buffer)
        else:
            report["errors"]["connection_state"] = c.get_last_error()
    else:
        report["errors"]["session_id"] = c.get_last_error()
    report["window_station"] = object_name(user.GetProcessWindowStation())
    report["thread_desktop"] = object_name(user.GetThreadDesktop(kernel.GetCurrentThreadId()))
    desktop = user.OpenInputDesktop(0, False, 1)  # DESKTOP_READOBJECTS only.
    try:
        report["input_desktop"] = object_name(desktop)
    finally:
        if desktop and not user.CloseDesktop(desktop):
            report["errors"]["close_input_desktop"] = c.get_last_error()
    report["monitors"] = user.GetSystemMetrics(80)  # SM_CMONITORS.
    return report


def require_interactive_desktop(report: dict) -> None:
    """Require the standard connected Windows desktop used by the native harness."""
    if report.get("session_id") in {None, 0}:
        raise RuntimeError("renderer benchmark requires an interactive Windows session")
    if report.get("connection_state") != 0:
        raise RuntimeError("renderer benchmark requires a connected, active Windows session")
    for key, expected in (
        ("window_station", "winsta0"),
        ("thread_desktop", "default"),
        ("input_desktop", "default"),
    ):
        observed = report.get(key, {}).get("name")
        if not isinstance(observed, str) or observed.lower() != expected:
            raise RuntimeError(
                f"renderer benchmark requires {key} {expected}; observed {observed!r}"
            )
    if report.get("monitors", 0) < 1:
        raise RuntimeError("renderer benchmark requires an attached monitor")
