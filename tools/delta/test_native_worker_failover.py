"""Host-side tests for the worker failover controller (its lease rewrite is stubbed)."""

import importlib
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
failover = importlib.import_module("native_worker_failover_v1")


class FakeRecord:
    def __init__(self, scope):
        self.scope, self.deadline_tick = scope, 777


class FakeGrant:
    def __init__(self, scope):
        self.record, self.mapping = FakeRecord(scope), 1


class FakeWorker:
    def __init__(self, pid, alive=True, code=None):
        self.pid, self._alive, self.returncode, self._handle = pid, alive, code, pid * 10

    def poll(self):
        return None if self._alive else self.returncode


def test_worker_ready_matches_only_the_named_pid():
    rows = [{"local_worker_ready": True, "pid": 7}, {"frame_batch": True}]
    assert failover.worker_ready(rows, 7) and not failover.worker_ready(rows, 8)
    assert not failover.worker_ready([], 7)


def test_check_keeps_a_living_worker():
    report, saves = {}, []
    f = failover.WorkerFailover(
        start_worker=lambda n: (_ for _ in ()).throw(AssertionError("not called")),
        leases=[],
        grant=lambda w: None,
        after_restart=lambda w: None,
        stdout_path=Path("missing"),
        process_identity=lambda h: h,
        report=report,
        save=lambda: saves.append(1),
    )
    worker = FakeWorker(5)
    assert f.check(worker) is worker and report == {} and not saves


def test_check_replaces_a_dead_worker_and_regrants(tmp_path, monkeypatch):
    log = tmp_path / "worker.stdout.jsonl"
    report, granted, restarted, saves = {}, [], [], []
    replacement = FakeWorker(9)

    def start_worker(generation):
        assert generation == 2
        log.write_text(json.dumps({"local_worker_ready": True, "pid": 9}) + "\n")
        return replacement

    class FakeProcessGrant:
        def __init__(self, handle):
            self.handle, self.restored, self.active = handle, False, True

    permissions = [FakeProcessGrant(50), FakeProcessGrant(50), FakeProcessGrant(7)]
    f = failover.WorkerFailover(
        start_worker=start_worker,
        leases=[FakeGrant(1), FakeGrant(2)],
        grant=granted.append,
        permissions=permissions,
        after_restart=restarted.append,
        stdout_path=log,
        process_identity=lambda h: ("identity", h),
        report=report,
        save=lambda: saves.append(1),
        limit=1,
    )
    rewritten = []

    def fake_regrant(grant, identity, milliseconds):
        assert identity == ("identity", 90) and milliseconds == 1200000
        rewritten.append(grant.record.scope)
        return 111

    monkeypatch.setattr(failover, "regrant", fake_regrant)
    assert f.check(FakeWorker(5, alive=False, code=1)) is replacement
    assert granted == [replacement] and restarted == [replacement]
    record = report["worker_restarts"][0]
    assert record["exit_code"] == 1 and record["restart"] == 1 and record["pid"] == 9
    # The compositor lease is rewritten for the new worker; the indirect host's keeps its deadline.
    assert record["deadlines"] == [111, 777] and rewritten == [1] and "restored" in record and saves
    # The dead worker's two grants are dropped, the controller's own stays.
    assert record["grants_dropped"] == 2 and [g.handle for g in permissions] == [7]
    # The limit is per admission: a second loss ends it.
    try:
        f.check(FakeWorker(9, alive=False, code=0))
    except AssertionError as error:
        assert "stopped 2 times" in str(error)
    else:
        raise AssertionError("second loss should end the admission")
