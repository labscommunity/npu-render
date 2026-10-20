# Turbo renderer: physical desktop trial

The corrected worker v26 trial passes finite desktop rendering, physical delivery,
controlled input, the shared test application and restoration. It still delivers
only **0.9 verified animation updates/s** during a ten-second 30 Hz schedule.
**Full desktop offload, GPU relief and a Tahoma benefit remain unqualified.**

This uses the unchanged, uninstrumented driver package v19 and the qualified
turbo worker. It is a separate run from the preceding 0.8/s worker v24 trial,
not a matched desktop A/B experiment.

## Completed physical trial

`ptl-combined-desktop-v16`, registration v53, native admission v74 and live v64
pass. The presentation interval is 550 seconds.

- 1,590 acknowledged hardware child operations; one generation, zero faults
  and zero retries.
- 110 physical submissions match their source frames exactly, with 95 distinct
  source pixel hashes.
- All forty controlled inputs are correlated to display observations. The
  shared normally started test application passes, followed by continued DWM
  rendering.
- Twelve custom-model turbo import readbacks pass in the single child.
  Repeated imports for non-singleton solid/texture providers are valid; the
  established generation audit separately checks shared-provider uniqueness.
- The original topology and owned state are restored. Memory integrity remains
  enabled.

The unleased default-device observer still fails device creation with HRESULT
`0x887a0004`. The shared test application is admitted through the existing finite
lease. This is not broad ordinary-application compatibility.

| Warm input, 19 observations each | Median sent-to-matching-display | p95 / maximum |
| --- | ---: | ---: |
| Mouse | 91.1899 ms | 108.2341 ms |
| Keyboard | 89.0148 ms | 108.0171 ms |

The first mouse observation is 1,630.2191 ms and is excluded from the warm row.
With nineteen warm samples, the chosen p95 statistic equals the maximum.
These are display-counter correlations, not panel photon measurements.

The producer schedules 300 changes at 30 Hz without skipping any. Nine unique
colors are verified during the scheduled ten seconds; eleven are verified
including drain. The remaining 289 scheduled colors have no matching submission.
The physical GPU bridge still performs final presentation and observer readbacks.
Zero iGPU use is not claimed.

## Measured rendering bottleneck

The retained animation analysis partitions the ten-second interval:

| Stage | Time |
| --- | ---: |
| Parent work before child connection | 624.3303 ms |
| Child packet read | 514.5986 ms |
| Child work before execution | 1.7664 ms |
| Child rendering execution | 6,021.6281 ms |
| Parent work after child execution | 551.7379 ms |
| Outside serialized parent execution | 2,285.9387 ms |

There are 290 overlapping child requests. Their whole-request counters contain
4,004 NPU calls and zero backend compilation time; boundary-crossing request
counters are not apportioned to the interval. One complex shader accounts for
1,825.8262 ms of overlapping child time. These intervals include host work and
NPU waits, not measured NPU hardware utilization.

## Retained setup failure

The earlier coordinator v15 / live v63 / native admission v73 fails during
bootstrap. The copied shared-application helper still reads registration v51's
worker identity, while the running worker is v26. The exact-identity check rejects
the mismatch. Native recovery completes, with memory integrity enabled.

The corrected helper binds to the current registration v53. The failed reports,
sources and restoration evidence remain unchanged in this archive. No rendering
contract or identity check is disabled to make the second trial pass.

`artifact-sha256.json` records 282 artifacts. Remote originals total 7,751,000 bytes;
large files are stored with lossless gzip compression and both original and stored
hashes. The local animation analysis and its helper sources are included.
This finite result does not cover sustained use, sleep/wake, arbitrary apps,
video/games or concurrent Tahoma performance.
