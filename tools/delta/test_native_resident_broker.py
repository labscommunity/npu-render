"""Host-side tests for the resident broker's pure parts; its Windows imports are stubbed."""

import importlib
import json
import sys
import types
from pathlib import Path


def load():
    stub = types.ModuleType("native_session_broker_v3")
    for name in ("C", "W", "adv", "k", "obj", "process_sid", "require", "ProcessGrant"):
        setattr(stub, name, object())
    grant = types.ModuleType("native_token_query_grant_v1")
    grant.TokenQueryGrant = object()
    sys.modules["native_session_broker_v3"] = stub
    sys.modules["native_token_query_grant_v1"] = grant
    sys.path.insert(0, str(Path(__file__).parent))
    return importlib.import_module("native_resident_broker_v1")


def test_summarize_counts_admissions_renewals_and_completion():
    broker = load()
    rows = [
        {"automatic_broker_ready": True, "pid": 10, "serve": True},
        {"automatic_consumer_admitted": True, "client_pid": 41, "admitted_total": 1},
        {"automatic_request_rejected": True, "reason": "sid"},
        {"automatic_consumer_admitted": True, "client_pid": 52, "admitted_total": 2},
        {"automatic_leases_renewed": True, "round": 1, "live_clients": 2, "pruned": 0},
        {"automatic_leases_renewed": True, "round": 2, "live_clients": 1, "pruned": 1},
        {"automatic_broker_completed": True, "passed": True, "reason": "stop-file"},
    ]
    summary = broker.summarize(rows)
    assert summary["ready"] and summary["admitted"] == 2 and summary["admitted_pids"] == [41, 52]
    assert summary["rejected"] == 1 and summary["renewal_rounds"] == 2
    assert summary["live_clients"] == 1 and summary["completed"] and summary["passed"]
    assert summary["reason"] == "stop-file"


def test_summarize_of_nothing_is_not_ready():
    broker = load()
    summary = broker.summarize([])
    assert not summary["ready"] and summary["admitted"] == 0 and summary["live_clients"] == 0
    assert not summary["completed"] and not summary["passed"] and summary["reason"] is None


def test_read_rows_skips_partial_lines(tmp_path):
    broker = load()
    path = tmp_path / "broker.stdout.jsonl"
    path.write_text(json.dumps({"automatic_broker_ready": True}) + "\n" + '{"automatic_consumer_ad')
    assert broker.read_rows(path) == [{"automatic_broker_ready": True}]
    assert broker.read_rows(tmp_path / "missing.jsonl") == []
