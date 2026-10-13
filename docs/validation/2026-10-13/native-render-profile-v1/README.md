# Combined optimizations with a configuration defect

Both 1,438-request runs preserve every pixel and rendering receipt, except the
previously qualified compact-coverage input-byte increase. However, this candidate
**did not activate the requested larger pipeline cache**. Its source retained the
production limits of 8 uniform, 24 textured-triangle and 4 clear programs. The
build report incorrectly advertised support for the `expanded` option. The raw
reports are preserved; their requested configuration is not evidence it took effect.

The same executable ran first with both optimizations disabled, then with compiled
program reuse and compact coverage enabled. With the actual smaller cache, replay
time fell from **687.089 to 530.684 seconds**, an observed **22.76% reduction** in
this pair. This does not compare directly with the earlier larger-cache experiments.
Clocks, temperature and background work were not fixed.

| Measurement | Both disabled | Both enabled |
| --- | ---: | ---: |
| Setup plus draw | 655.795 s | 501.527 s |
| Draw timer | 291.601 s | 306.297 s |
| Backend compile/load calls | 3,630 | 1,302 |
| Compiled programs reused from memory | 0 | 2,443 |
| Compact coverage calls | 0 | 13,992 |
| Wide coverage calls | 16,759 | 2,767 |
| Verified replies and acknowledgments | 1,438 | 1,438 |

The enabled run processes 51,063,808 compact samples. Its logical input accounting
increases by exactly eight bytes per sample; every other receipt field matches.
Compact programs are prepared lazily within execution, so the draw timer includes
some preparation that the wide path performs earlier. Summed setup plus draw is
the useful combined host-time comparison; neither timer is pure device time.

A clean Windows build passes 32 CPU suites and five NPU component stages. The
original replay comparison helper passes 87 boundary cases. The corrected helper
adds actual worker startup-policy checks and passes 107 cases. Running it against
this defective candidate rejects the missing policy evidence **before sending
the first rendering request**, despite the misleading build capability flag.
The expected rejection and owned-worker termination are retained separately.

Twenty-five remote artifacts were checked against SHA-256 and size receipts and
stored losslessly under `raw/`. Source hashes establish the actual smaller cache.
The source patch is relative to the qualified production native-client snapshot.
A subsequent candidate must report and verify the larger runtime capacities.
Neither physical desktop migration nor Tahoma throughput was tested here.
