"""Resident automatic admission beside one native desktop admission.

The one-shot interactive broker admits a single cued client and ends with it. In serve
mode (`nr_auto_interactive_broker.exe <worker pid> <suffix> <session> --serve <stop file>`)
the same binary keeps the session's publication current and admits every same-user
interactive client that opens the NPU adapter, renewing each admitted lease in place until
the client exits or the stop file appears. This module owns that process from the
bootstrap: it grants the console user query access to the worker and broker processes and
their tokens (the driver verifies both peers from the client), reads the broker's rows into
the admission report, and restores every grant when it closes.

`summarize` is pure and tested on the host; everything else needs the Windows broker module.
"""

import hashlib
import json
import subprocess
import time

from native_session_broker_v3 import C, ProcessGrant, W, adv, k, obj, process_sid, require
from native_token_query_grant_v1 import TokenQueryGrant


def read_rows(path):
    """Complete JSON rows of a stream another process is still appending to."""
    if not path.exists():
        return []
    result = []
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        if line.startswith("{") and line.endswith("}"):
            try:
                result.append(json.loads(line))
            except json.JSONDecodeError:
                pass
    return result


def summarize(broker_rows):
    """Counts for the report from the broker's stdout rows (pure)."""
    admitted = [x for x in broker_rows if x.get("automatic_consumer_admitted")]
    renewed = [x for x in broker_rows if x.get("automatic_leases_renewed")]
    completed = [x for x in broker_rows if x.get("automatic_broker_completed")]
    return dict(
        ready=any(x.get("automatic_broker_ready") for x in broker_rows),
        admitted=len(admitted),
        admitted_pids=sorted({x["client_pid"] for x in admitted}),
        rejected=sum(1 for x in broker_rows if x.get("automatic_request_rejected")),
        renewal_rounds=len(renewed),
        live_clients=renewed[-1]["live_clients"] if renewed else 0,
        completed=bool(completed),
        passed=bool(completed) and bool(completed[0].get("passed")),
        reason=completed[0].get("reason") if completed else None,
    )


class ResidentBroker:
    def __init__(
        self, transaction, worker, suffix, session, env, own_process, build_root, generation=1
    ):
        self.worker, self.suffix, self.session, self.env = worker, suffix, session, env
        self.own_process = own_process
        # A replacement worker (worker failover) gets a new broker beside the first one's evidence.
        suffix_dir = "" if generation == 1 else f"-{generation}"
        self.out = transaction / f"bootstrap/resident-broker{suffix_dir}"
        self.stop = self.out / "stop"
        self.process = None
        self.streams, self.grants, self.tokens = [], [], []
        build = obj(build_root / "registered-build-report.json")
        assert build["clean_build"] and build["contract_passed"]
        self.binary = build_root / "Release/nr_auto_interactive_broker.exe"
        digest = hashlib.sha256(self.binary.read_bytes()).hexdigest()
        assert digest == build["binary_sha256"][self.binary.name].lower()

    def grant(self, process, sid):
        handle = int(process._handle)
        self.grants.append(ProcessGrant(handle, sid))
        token = W.HANDLE()
        require(adv.OpenProcessToken(handle, 8 | 0x20000 | 0x40000, C.byref(token)))
        self.tokens.append(token)
        self.grants.append(TokenQueryGrant(token, sid))

    def start(self):
        self.out.mkdir(exist_ok=False)
        sid = process_sid(k.GetCurrentProcess())[1]
        self.grant(self.worker, sid)
        acl = subprocess.run(
            ["icacls.exe", str(self.out), "/grant", "*" + sid + ":(OI)(CI)M"],
            capture_output=True,
            timeout=10,
        )
        (self.out / "directory-acl.log").write_bytes(acl.stdout + acl.stderr)
        assert acl.returncode == 0
        for name in ("broker.stdout.jsonl", "broker.stderr.log"):
            self.streams.append((self.out / name).open("xb"))
        command = [str(self.binary), str(self.worker.pid), self.suffix, str(self.session)]
        command += ["--serve", str(self.stop)]
        self.process = subprocess.Popen(
            command,
            env=self.env,
            stdin=subprocess.DEVNULL,
            stdout=self.streams[0],
            stderr=self.streams[1],
            creationflags=subprocess.CREATE_NO_WINDOW,
        )
        self.own_process(int(self.process._handle), "resident-broker")
        self.grant(self.process, sid)
        deadline = time.perf_counter() + 5
        while not summarize(self.rows())["ready"]:
            alive = self.process.poll() is None
            assert alive and time.perf_counter() < deadline, "Resident broker did not publish"
            time.sleep(0.01)
        return self.process.pid

    def rows(self):
        return read_rows(self.out / "broker.stdout.jsonl")

    def poll(self):
        assert self.process.poll() is None, "Resident broker exited during the admission"
        return summarize(self.rows())

    def close(self):
        report = dict(completed=False)
        try:
            if self.process is not None and self.process.poll() is None:
                self.stop.write_text("stop\n")
                try:
                    self.process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    self.process.kill()
                    self.process.wait(timeout=5)
                    report["killed"] = True
            if self.process is not None:
                report["exit_code"] = self.process.returncode
            report.update(summarize(self.rows()))
        finally:
            restored = []
            for grant in reversed(self.grants):
                try:
                    grant.restore()
                    restored.append(grant.restored)
                except Exception as error:
                    restored.append(False)
                    report.setdefault("restore_errors", []).append(repr(error))
            for token in self.tokens:
                k.CloseHandle(token)
            for stream in self.streams:
                stream.close()
            report["grants_restored_exactly"] = bool(self.grants) and all(restored)
        report["completed"] = True
        return report
