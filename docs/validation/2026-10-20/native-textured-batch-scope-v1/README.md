# Texture batching: recorded-workload scope

The fast textured rectangle prototype does not yet represent the expensive
texture state in the full recorded workload. Requests binding linear filtering
or multiple textures account for **77.70% of child request time** in the two
worker31 replays. This is an implementation-scope check, not a new benchmark.

| Bound state | Distinct requests | Child time across B1 and B2 |
| --- | ---: | ---: |
| Clear | 201 | 1,252.85 ms |
| No texture | 186 | 30,936.12 ms |
| One point-filtered RGBA/BGRA texture | 147 | 11,876.11 ms |
| One linearly filtered texture | 252 | 115,704.97 ms |
| Multiple textures | 186 | 37,839.07 ms |

The source is the retained 972-request metadata inventory joined to both complete
worker31 replays. All 1,944 timed request identities independently match their
original ABI, flags, dimensions, vertex counts and pixel-shader hash. Summed times
reconcile to both replay totals. The artifact manifest pins the analyses and
reports; the source artifacts remain in their original archives.

The component implements solids or one-to-one point texture copies, copy or
source-over blending, and channel masks. Even the first three rows are **not
proven eligible**: shader arithmetic, modulation, geometry, sampling coordinates,
clipping and resource dependencies still require validation. Bound textures can
be unused; eliminating or simplifying their semantics needs an explicit proof.
This conservative screen must not become a runtime shader classifier.

The time totals include host orchestration and NPU waits. They are two replay
runs, not independent experiments per request, physical frame timings, or a
forecast of batching savings. No desktop or Tahoma work is run by this analysis.

The next integration must establish real draw eligibility and owned resource
versions, then support the dominant filtered and multi-texture work or drain it
in order to the existing NPU renderer. The 14.58 ms simple-component result cannot
be substituted for the cost of a complete desktop frame.
