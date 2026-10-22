"""Replace a native desktop admission's NPU worker when it ends.

The bootstrap owns one worker (parent plus provider child) per admission. When it ends
for any reason the compositor's next request fails, and the driver waits for this
controller to publish a replacement (indexed_exchange.hpp, worker failover): a new
worker on the same private pipe, the same process grants, and every lease rewritten
with the new worker's identity and fresh ticks. A rewrite is not a renewal, so a
cached admission is replaced rather than extended, which is what a consumer binding to
a new worker needs. The bootstrap's callback restarts the resident broker, which pins
the worker. Up to `limit` restarts per admission; beyond that the admission fails as it
did before, and every restart is recorded in the report.
"""

import ctypes as C
import datetime
import json
import os
import time

SCOPE_MAXIMUM_MS = {0: 120000, 1: 2400000, 2: 2400000, 3: 120000}
FILE_MAP_WRITE = 2

k = C.windll.kernel32 if os.name == "nt" else None
if k is not None:
    k.MapViewOfFile.restype = C.c_void_p
    k.MapViewOfFile.argtypes = [C.c_void_p, C.c_uint32, C.c_uint32, C.c_uint32, C.c_size_t]
    k.UnmapViewOfFile.argtypes = [C.c_void_p]
    k.GetTickCount64.restype = C.c_uint64


def now_iso():
    return datetime.datetime.now(datetime.UTC).isoformat()


def read_rows(path):
    """Complete JSON rows of a log another process is still appending to."""
    if not path.exists():
        return []
    result = []
    for line in path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
        if line.startswith("{") and line.endswith("}"):
            try:
                result.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return result


def worker_ready(rows, pid):
    """True once the worker with `pid` has written its ready row (pure)."""
    return any(x.get("local_worker_ready") and x.get("pid") == pid for x in rows)


def regrant(grant, worker_identity, milliseconds):
    """Rewrite the grant's mapping with a new worker and fresh ticks; returns the new deadline."""
    record = grant.record
    maximum = SCOPE_MAXIMUM_MS[record.scope]
    assert 0 < milliseconds <= maximum, (milliseconds, maximum)
    assert grant.mapping, "grant is closed"
    record.worker = worker_identity
    now = k.GetTickCount64()
    record.issued_tick = now
    record.deadline_tick = now + milliseconds
    view = k.MapViewOfFile(grant.mapping, FILE_MAP_WRITE, 0, 0, C.sizeof(record))
    assert view, "lease mapping view failed"
    try:
        C.memmove(view, C.byref(record), C.sizeof(record))
    finally:
        k.UnmapViewOfFile(view)
    return record.deadline_tick


class WorkerFailover:
    def __init__(
        self,
        *,
        start_worker,
        leases,
        grant,
        after_restart,
        stdout_path,
        process_identity,
        report,
        save,
        limit=3,
        ready_seconds=20,
        grant_ms=1200000,
        permissions=None,
    ):
        self.start_worker, self.leases, self.grant = start_worker, leases, grant
        self.after_restart, self.stdout_path = after_restart, stdout_path
        self.permissions = permissions if permissions is not None else []
        self.process_identity, self.report, self.save = process_identity, report, save
        self.limit, self.ready_seconds, self.grant_ms = limit, ready_seconds, grant_ms
        self.restarts = []

    def drop_grants(self, handle):
        """Forget the grants held on an exited process; returns how many."""
        dead = [g for g in self.permissions if getattr(g, "handle", None) == handle]
        for g in dead:
            g.restored = True
            g.active = False
            self.permissions.remove(g)
        return len(dead)

    def regrant_all(self, identity):
        """Rewrites the grants whose consumer talks to the worker (the compositor's and any
        same-session client's); the indirect host's lease (scope 2) never names the worker it
        would re-bind to, so it keeps renewing unchanged."""
        deadlines = []
        for g in self.leases:
            if g.record.scope == 2:
                deadlines.append(int(g.record.deadline_tick))
                continue
            deadlines.append(
                int(regrant(g, identity, min(self.grant_ms, SCOPE_MAXIMUM_MS[g.record.scope])))
            )
        return deadlines

    def check(self, worker):
        """Returns the worker to keep polling: the same one while it lives, else its replacement."""
        if worker.poll() is None:
            return worker
        n = len(self.restarts) + 1
        assert n <= self.limit, f"NPU worker stopped {n} times; the admission ends"
        record = dict(lost=now_iso(), exit_code=worker.returncode, restart=n)
        self.restarts.append(record)
        self.report["worker_restarts"] = self.restarts
        # The grants on the dead worker's process object have nothing left to restore (trial 30:
        # their restoration failed with an invalid handle and marked the admission as not passed).
        record["grants_dropped"] = self.drop_grants(int(worker._handle))
        self.save()
        started = time.perf_counter()
        replacement = self.start_worker(n + 1)
        self.grant(replacement)
        deadline = started + self.ready_seconds
        while not worker_ready(read_rows(self.stdout_path), replacement.pid):
            alive = replacement.poll() is None
            assert alive and time.perf_counter() < deadline, "replacement NPU worker was not ready"
            time.sleep(0.02)
        record["deadlines"] = self.regrant_all(self.process_identity(int(replacement._handle)))
        self.after_restart(replacement)
        record.update(pid=replacement.pid, ready_ms=(time.perf_counter() - started) * 1000)
        record["restored"] = now_iso()
        self.save()
        return replacement
