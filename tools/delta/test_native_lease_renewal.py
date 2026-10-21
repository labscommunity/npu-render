"""The renewal planner never renews more often than a grant is worth."""

import native_lease_renewal_v1 as renewal

GRANT = 2340_000
MARGIN = 600_000


def test_no_renewal_outside_margin():
    assert (
        renewal.plan_renewal(1_000_000, 1_000_000 + MARGIN + 1, grant_ms=GRANT, margin_ms=MARGIN)
        == 0
    )


def test_full_grant_inside_margin():
    assert (
        renewal.plan_renewal(1_000_000, 1_000_000 + MARGIN, grant_ms=GRANT, margin_ms=MARGIN)
        == GRANT
    )


def test_grant_capped_by_run_end():
    now = 1_000_000
    step = renewal.plan_renewal(
        now, now + 500_000, grant_ms=GRANT, margin_ms=MARGIN, stop_after_tick=now + 900_000
    )
    assert step == 960_000


def test_capped_grant_that_cannot_leave_the_margin_renews_once():
    """After one capped renewal the deadline sits at run end + 60 s; no further renewal is due."""
    now = 1_000_000
    stop = now + 500_000
    first = renewal.plan_renewal(
        now, now + 400_000, grant_ms=GRANT, margin_ms=MARGIN, stop_after_tick=stop
    )
    assert first == 560_000
    deadline = now + first
    later = now + 40_000
    again = renewal.plan_renewal(
        later,
        deadline,
        grant_ms=GRANT,
        margin_ms=MARGIN,
        stop_after_tick=stop,
        last_renewal_tick=now,
    )
    assert again == 0


def test_spacing_between_renewals():
    now = 1_000_000
    assert (
        renewal.plan_renewal(
            now, now + 100_000, grant_ms=GRANT, margin_ms=MARGIN, last_renewal_tick=now - 10_000
        )
        == 0
    )
