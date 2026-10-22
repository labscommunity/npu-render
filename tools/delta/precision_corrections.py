"""Replay-only exact references for separately qualified numerical profiles."""

import hashlib
import json

import precision_corrections_v1

TICKETS = {58, 196, 302, 363, 820}


def validate(data, digest, requests, responses):
    assert hashlib.sha256(data).hexdigest() == digest
    report = json.loads(data)
    assert report["schema"] == 2 and report["qualified"] and report["original_capture_unchanged"]
    original = bytes.fromhex(report["prior_manifest_hex"])
    result = precision_corrections_v1.validate(
        original, report["prior_manifest_sha256"], requests, responses
    )
    assert (
        report["prior_manifest_sha256"]
        == "5afd2995cb49f64f3bb6d7d3ce75e30b1fb7d6f989261027425cd76768d70c48"
    )
    assert report["reason"] == "explicit-fp32-sampler-profile-exact-independent-reference"
    cases = report["cases"]
    assert len(cases) == 5 and {x["ticket"] for x in cases} == TICKETS
    by_ticket = {x["ticket"]: x for x in requests}
    for case in cases:
        ticket = case["ticket"]
        assert ticket not in result and ticket in responses
        assert case["request_sha256"] == by_ticket[ticket]["sha256"].lower()
        assert case["legacy_pixel_sha256"] == responses[ticket]["pixels_sha256"].lower()
        assert case["pixel_bytes"] == responses[ticket]["pixel_bytes"]
        assert (
            case["corrected_pixel_sha256"]
            == case["independent_graph_pixel_sha256"]
            != case["legacy_pixel_sha256"]
        )
        assert (
            case["exact_comparisons"] == 4
            and case["legacy_reference_verified"]
            and case["independent_phase_analysis_exact"]
        )
        assert (
            case["profile"] == "explicit-fp32-sampler-staging"
            and case["claimed_old_renderer_bug_fixed"] is False
        )
        for key in (
            "request_sha256",
            "legacy_pixel_sha256",
            "corrected_pixel_sha256",
            "independent_graph_pixel_sha256",
        ):
            assert len(case[key]) == 64 and all(c in "0123456789abcdef" for c in case[key])
        result[ticket] = case
    return result
