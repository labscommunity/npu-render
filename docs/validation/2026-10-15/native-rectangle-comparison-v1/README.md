# Direct provider comparison and vector-kernel qualification

The direct comparison does **not** qualify a bit-exact replacement or a desktop
speed improvement. Each run alternates baseline/custom order across 112 changing
64×64 rectangles, checking 1,835,008 components per path. The custom scalar kernel
matches the independent float32 reference exactly. The unchanged native v45 NPU
provider differs in 985 components; its CPU configuration differs in 154. Both
comparisons fail their exact-equality gate. Vertex values match independently;
the inspected pixel differences lie near output rounding thresholds. This does
not establish that every discrepancy has the same cause.

The comparison retains the current provider's crop optimization. The first four
frames per style are labeled warmup, but some later inputs trigger lazy model
preparation. The reported baseline merger timer excludes general model preparation
and vertex/projection; custom timing includes input copy, inference and output
conversion, starting from supplied projected bounds. Total draw time is also
retained. These timing scopes must not be treated as equivalent full desktop draws.

In the repeated NPU comparison, baseline/custom merger medians are 3.95/4.60 ms
(point copy), 5.03/4.19 ms (bilinear copy), 3.92/3.25 ms (point source-over), and
5.51/4.18 ms (bilinear source-over). Some existing crop cases are much faster than
these mixed-case medians. The earlier trial differs in relative point-blend timing;
no robust speedup is claimed. Numerical failures also prevent promotion.

The first vector candidate returns incorrectly encoded FP16 values. Replacing its
output conversion clears that symptom, but covered pixels remain incorrect. A
third candidate keeps scalar quantization/output conversion around vector sampling
and blending. It passes all 12,582,912 custom components across the six qualified
CMX/DDR shapes and the before/after original-graph checks. A separate scalar
candidate that increments pixel coordinates also passes the same suite. Their
diagnostic times are not a counterbalanced performance comparison. Both failed
vector candidates and the initial Windows `min`/`max` macro build failure are retained.

The retained slow desktop shader (`d305ca5e…2777a1a`, 157 words) uses derivatives
and four samples before color modulation. The one-sample rectangle component does
not yet replace that shader or its coverage/interpolation/merger requests.
Further work targets that actual path, complete timing and live integration.

All 107 raw files (1,250,290 bytes) match remote receipts, including source,
build/library hashes, failed and successful qualification results, comparison
outputs, and hashes of reproducible synthetic fixtures. No desktop pixels,
compiled device binaries, or proprietary tools are included. No Tahoma test or
live provider replacement occurred.
