# Ordered rectangle rendering: targeted gain, unchanged full replay

Worker 22 routes eligible quad batches through the existing NPU rectangle
provider, preserving whole-batch geometry preflight and sequential blending.
All 972 captured images and all 3,888 A/B/B/A images match worker 19 exactly.
Every normal replay uses one provider generation with no failure.

| Phase | Worker | All parent requests | Five targeted requests | Replay wall time |
| --- | --- | ---: | ---: | ---: |
| A1 | 19 | 113.708 s | 1.909 s | 204.925 s |
| B1 | 22 | 114.082 s | 1.033 s | 204.619 s |
| B2 | 22 | 113.810 s | 1.095 s | 204.326 s |
| A2 | 19 | 114.163 s | 1.962 s | 204.408 s |

Across all 1,944 paired requests, aggregate time is effectively unchanged:
227.871 versus 227.892 seconds, with 1,001 slower pairs. The existing five-percent
full-replay promotion threshold is not met.

The [targeted analysis](analysis/targets.json) includes every retained use of
the changed route: five draws containing nineteen rectangles. Their ten pairs
fall from 3.871 to 2.128 seconds, **45.03% less**, with no slower target pair.
Complete-draw provider calls fall from 380 to 176, including preflight work.
This is useful component evidence, not a measured desktop or Tahoma benefit.
Provider recovery and a live candidate trial are separate gates.

The first selected replay preserved all 106 images but its qualification failed:
the selection omitted every accepted quad batch. That failed v1 report remains.
The corrected v2 selection adds tickets 467, 587, 653, 719 and 775; all 111 images
and nineteen rectangle executions pass. The worker binary is unchanged between
these attempts. Only `indexed_pipeline.hpp` and the new `ptl_quad_batch.hpp`
differ from worker 19; kernels, model bytes and provider libraries are unchanged.

The [manifest](artifact-sha256.json) covers 124 artifacts, including 119 checked
against remote hashes, three local preparation/collection sources and two local
analysis artifacts. No compiled model or executable is redistributed.
