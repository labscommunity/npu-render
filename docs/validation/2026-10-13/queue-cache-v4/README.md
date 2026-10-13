# Ordered desktop workload: queue and pipeline cache experiments

All three configurations replay the complete 1,438-request v120 desktop trace
on Delta's physical Panther Lake NPU, with identical pixels and execution
receipts. Expanded pipeline retention completes in 641.906 seconds versus
888.312 seconds for the original policy, a 27.74% reduction in this first
comparison. This is recorded-workload timing; desktop responsiveness and Tahoma
performance were not tested. Run order and warming effects are not controlled.

| Configuration | Elapsed seconds | Draw seconds | Setup + draw seconds | Model setup calls |
|---|---:|---:|---:|---:|
| Original policy | 888.312 | 325.586 | 728.692 | 3,611 |
| Shared queue, original retention | 873.656 | 319.630 | 712.837 | 3,611 |
| Shared queue, expanded retention | 641.906 | 301.571 | 482.117 | 1,711 |

Every model setup reports a disk-cache hit. Retaining live pipelines avoids setup
work that persists despite that cache. Expanded retention doubles the uniform and
triangle limits to 16 and 48; the clear limit remains four. Shared queue alone
changes elapsed time by 1.65%, which is insufficient to establish a useful gain.
The private `NPU_SHARED_COMMON_QUEUE` property is pinned to the exact OpenVINO
runtime and its value is read back. Hardware queue counts are not measured.

[report.json](report.json) verifies worker attribution, all request hashes,
pixel/receipt equivalence, timing sums and sampled process memory. Wall time
includes filesystem exchange, polling and report persistence. Setup-plus-draw
timing uses a coarse host clock. The memory monitor does not cover every instant.
[experiment.patch](experiment.patch) records the isolated source changes relative
to renderer commit `233d03880981cc7309a4cd27c163acc6ce36a8b1`; the default renderer
policy is unchanged. Clean build, six CPU contracts, NPU property tests, independent
component oracles and three small replay variants precede the full comparison.

Two earlier replays stop because Windows temporarily denies file access: one
reply read and one progress-report replacement. Their raw results are retained;
the worker successfully completes the current request in each case. The updated
helper bounds access-denial retries to five seconds. Its test uses real Windows
share locks and verifies recovery, permanent denial and unrelated-error behavior.

A separate fresh-executor probe reproduces three later point-copy draws in three
NPU calls each, versus 1,538 calls on the retained general path, with identical
full output pixels. This confirms that a prior partial draw unnecessarily keeps
later compatible copies on the slower path. This build does not contain a fix;
the probe's fresh-process times are not a controlled latency comparison.

Raw source hashes, normalized UTF-8 copies, scripts and failed runs are retained
here. The original 4.4 GB packet trace remains in the private ignored archive,
indexed by the v120 manifest. No new display qualification, physical desktop
migration, or Tahoma improvement is established by this experiment.
