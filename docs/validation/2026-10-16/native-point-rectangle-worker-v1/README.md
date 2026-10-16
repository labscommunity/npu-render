# Rectangle routing and combined NPU worker

Worker v14 passes all 972 retained requests and preserves every output from
workers v12 and v13. Its explicit numerical profile is unchanged: 957 original
capture hashes, 14 previously qualified reference hashes, and one request without
an original reference. All 972 outputs also match the immediately preceding
worker. There is one provider generation, no provider failure, and 1,944 correlated
parent/child timing records.

The worker combines the five-sample shader with direct point rectangles. Of 37
rectangle draws, 28 use the direct kernel and nine retain the existing NPU crop
path. Each component shares one model import. Ten draws use the complex shader,
463 use the program kernel, and 126 use shared geometry. Worker v13, also retained,
always tries the crop first and executes six direct rectangles and 31 crops.

The refined rectangle policy uses blend semantics and bounded row count. Plain
copies, opaque-alpha cases, and blended draws fitting one crop row batch try the
existing qualified crop. Larger blended draws use the direct kernel. The crop's
mapping proof still controls its own admission; unsupported mappings use the
direct NPU kernel. The policy has no screenshot or shader-hash special cases.
Only this routing header changes between workers v13 and v14. Kernel arithmetic,
driver package, linked libraries, and image contracts remain unchanged.

All four variants of all 37 admitted captures match both the old NPU renderer
and its rectangle graph executed on CPU: 148 complete-image comparisons. All
original image hashes remain unchanged. Across 111 warm pairs, summed complete
draw time is 12,483.229 ms on the old renderer and 5,525.198 ms on the candidate.
Sixteen individual pairs are slower. The 148 variants exercise 112 direct-kernel
and 36 preserved-crop routes. These are component measurements, not desktop
latency, frame rate, GPU capacity, or Tahoma results.

Selected and full worker replays pass for both revisions. The archive preserves
234 source and evidence artifacts and excludes private image pixels, compiled
binaries, model blobs, and caches. The underlying kernel and independent
233-fixture qualification are in the [rectangle component archive](../native-point-rectangle-v1/README.md).
The [complex-worker archive](../native-complex-worker-v1/README.md) retains the
explicit FP32 profile and its independent image references. Registered graphics
and live desktop integration are subsequent gates.
