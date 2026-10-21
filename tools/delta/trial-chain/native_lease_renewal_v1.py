"""Renew native probe leases in place while a desktop admission runs.

A LeaseGrant (native_session_broker) publishes a 224-byte Lease record in a file
mapping the consumer reads once and re-reads as its cached deadline approaches
(native_probe_lease.hpp, Admission::refresh). Renewal advances issued_tick and
deadline_tick in that order, each store an aligned 8-byte write, so a reader never
sees a record whose deadline precedes its issue tick; nothing else changes, which
is what consumers and the worker require of a renewal. Every grant stays within
the scope's maximum (40 minutes for the compositor and the indirect host), so an
abandoned controller still expires on schedule.
"""

import ctypes as C
import datetime
import subprocess

k = C.windll.kernel32
k.MapViewOfFile.restype = C.c_void_p
k.MapViewOfFile.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_size_t]
k.UnmapViewOfFile.argtypes = [C.c_void_p]
k.GetTickCount64.restype = C.c_uint64
FILE_MAP_WRITE = 2
SCOPE_MAXIMUM_MS = {0: 120000, 1: 2400000, 2: 2400000, 3: 120000}


def extend(grant, milliseconds):
    """Move the grant's deadline to now + milliseconds; returns the new deadline tick."""
    record = grant.record
    maximum = SCOPE_MAXIMUM_MS[record.scope]
    assert 0 < milliseconds <= maximum, (milliseconds, maximum)
    assert grant.mapping, "grant is closed"
    lease_type = type(record)
    view = k.MapViewOfFile(grant.mapping, FILE_MAP_WRITE, 0, 0, C.sizeof(record))
    assert view, "lease mapping view failed"
    try:
        now = k.GetTickCount64()
        deadline = now + milliseconds
        C.c_uint64.from_address(view + lease_type.issued_tick.offset).value = now
        C.c_uint64.from_address(view + lease_type.deadline_tick.offset).value = deadline
        record.issued_tick = now
        record.deadline_tick = deadline
    finally:
        k.UnmapViewOfFile(view)
    return deadline


def rearm_recovery_task(task_name, seconds):
    """Re-register the recovery task's one-shot trigger `seconds` from now; startup trigger kept."""
    script = (
        f"$t=@((New-ScheduledTaskTrigger -Once -At (Get-Date).AddSeconds({int(seconds)})),"
        "(New-ScheduledTaskTrigger -AtStartup));"
        f"Set-ScheduledTask -TaskName '{task_name}' -Trigger $t | Out-Null"
    )
    result = subprocess.run(
        ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode == 0, result.stderr[-400:]


class Renewal:
    """Keeps every grant and the recovery task ahead of a moving deadline.

    `poll` is cheap to call each monitor iteration: it renews when less than
    `margin_seconds` of the current grant remains, in steps of `grant_seconds`
    (capped by each scope's maximum), and records every renewal for the report.
    """

    def __init__(
        self,
        grants,
        task_name,
        *,
        grant_seconds=2340,
        margin_seconds=600,
        recovery_slack_seconds=120,
    ):
        self.grants = list(grants)
        self.task_name = task_name
        self.grant_seconds = grant_seconds
        self.margin_seconds = margin_seconds
        self.recovery_slack_seconds = recovery_slack_seconds
        self.renewals = []

    def poll(self, stop_after_tick=None):
        now = k.GetTickCount64()
        soonest = min(g.record.deadline_tick for g in self.grants)
        if soonest - now > self.margin_seconds * 1000:
            return False
        step = self.grant_seconds * 1000
        if stop_after_tick is not None:
            step = min(step, max(60000, stop_after_tick - now + 60000))
        deadlines = []
        for grant in self.grants:
            deadlines.append(extend(grant, min(step, SCOPE_MAXIMUM_MS[grant.record.scope])))
        rearm_recovery_task(
            self.task_name, min(deadlines) // 1000 - now // 1000 + self.recovery_slack_seconds
        )
        self.renewals.append(
            dict(
                observed=datetime.datetime.now(datetime.UTC).isoformat(),
                remaining_before_s=(soonest - now) / 1000,
                granted_s=step / 1000,
                deadlines=[int(d) for d in deadlines],
            )
        )
        return True
