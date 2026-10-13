# Matched compact coverage comparison

The same `compact-runtime-v1` worker completed four full desktop-trace replays in
off/on/on/off order. All 5,752 requests preserved exact output pixels and coverage.
Compact coverage reduced average total replay time by **5.47%** and aggregate
setup plus draw time by **6.35%** in this sequence. Both enabled runs were faster
than both disabled runs. This is a captured-workload result; physical desktop
performance and Tahoma were not tested.

| Run | Compact coverage | Replay time | Summed setup + draw | Model setups / disk hits |
| --- | --- | ---: | ---: | ---: |
| A1 | Off | 576.911 s | 466.746 s | 1,730 / 1,730 |
| B1 | On | 545.248 s | 438.254 s | 1,775 / 1,775 |
| B2 | On | 548.029 s | 439.577 s | 1,775 / 1,775 |
| A2 | Off | 579.612 s | 470.572 s | 1,730 / 1,730 |

Each run used the same binary, 1,438-request v120 trace, graph cache directory,
explicit NPU selection, shared-queue option, and expanded pipeline capacities.
Every run made 57,221 provider calls. Enabled runs used compact arithmetic for
13,992 coverage calls and the original wide arithmetic for 2,767. Disabled runs
used the wide path for all 16,759 coverage calls. The only receipt difference
was exactly eight additional input bytes per compact sample. Every other
receipt field matched. All four workers exited successfully with empty stderr.

The earlier experiment's draw-time comparison was misleading because the opt-in
path constructs coverage programs lazily during execution. Its draw timer includes
setup that the disabled path performs before drawing. Draw time averaged 294.608
seconds enabled and 284.937 seconds disabled, while the combined setup-and-draw
measurement improved. The source change explains this accounting difference;
these draw-time sums do not demonstrate a rendering regression.

Harness v8 starts the high-resolution controller timer before request publication,
writes a progress snapshot every 64 completed requests, and removes each owned
request/reply file only after verifying it. It retains per-request hashes,
receipts and worker telemetry. This avoids repeatedly rewriting the full growing
report and scanning thousands of completed exchange files. Absolute replay times
must not be directly compared with the earlier harness. Both enabled and disabled
runs here use v8. Its receipt comparison passed 55 boundary cases before execution.

The worker's setup-and-draw timer still uses `GetTickCount64`; aggregate values
are reported with that limitation. This is one ABBA sequence, not a statistical
confidence interval. Temperature, clocks and background work were not fixed.
All model loads reported disk cache hits. The reverse ordering reduces a simple
warming-order confound but does not eliminate every source of variation.

Thirteen artifacts were verified against remote SHA-256 and byte counts. Raw
reports and worker logs are losslessly compressed under `raw/`; `raw-manifest.json`
records original and compressed hashes. Curation independently checked every
request against A1, including the exact input-byte accounting. Original source
packets remain on the private host; v8's verified exchange copies were removed.

This does not promote the compact path into the production renderer or establish
live desktop responsiveness. It supports continuing the qualified opt-in path
alongside investigation of program loading and request overhead.
