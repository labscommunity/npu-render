# Complete bounded-program draw qualification

Integration v5 passes all 381 admitted captures across four original/changed-image
variants each: 1,524 complete comparisons and 7,141,341,808 byte components. Every
original captured image matches the qualified worker's output. There is one shared
program-model import per comparison process. Candidate vertex, projection,
coverage, interpolation, sampling, shader execution and blending run on the NPU.

The selected eleven-program suite passes before the full run. Four captures
(tickets 18, 29, 932 and 943) use the separately validated FP32 alpha-filter
reference for all variants. Their old NPU graph differs on generated images;
those differences remain recorded in `pairs`, while `reference_checks` verifies
exact equality to the independent reference. The original image still matches in
all four. The reference is test-only and is absent from the worker source set.
The [arithmetic investigation](../native-program-integration-v1/README.md) proves
why historical equality alone would accept an FP16 precision loss in these cases.
No tolerance or additional original-image correction is introduced.

| Component measurement | Original NPU path | Shared program candidate |
| --- | ---: | ---: |
| Sum of all timed draws | 338.168 s | 116.825 s |
| Sum excluding each case's first iteration | 193.982 s | 84.852 s |
| Provider requests across all comparisons | 78,600 | 40,520 |

The warm summed reduction is about 56.3%. In 114 of 1,524 individual comparisons,
the candidate is slower. These are finite component comparisons with alternating
order, not a matched desktop or Tahoma benchmark. Setup and the test reference are
outside the timed candidate draw. The first iteration can include original graph
compilation, so the warm sum is shown separately. No concurrent graphics or NPU
experiment ran during these comparisons.

The bounded admission keeps existing NPU rectangle handling and explicitly
rejects unsupported texture extents, filters and blend modes. It accepts 381 of
the 605 structurally lowerable draws, not every captured operation. The complete
worker replay and live integration must qualify the combined paths separately.
All per-case logs, failure checks, source hashes, output hashes and the complete
runner are preserved. Pixel images and compiled models remain on Delta.
