# Larger generic batches and compiled-model sharing

Two independent candidates pass NPU correctness checks. Worker v35 admits larger
generic raster batches; worker v36 reuses compiled models with an optional
512-entry cache. Neither candidate is promoted to the live desktop in this
archive. Their combination and matched performance remain unqualified here.
Full desktop offload and a Tahoma benefit remain unfinished.

## Generic batch candidate

Worker v35 derives from v34 and keeps the existing captured compositor
specializations. `NR_RENDER_GENERIC_BATCH` optionally selects 4,096, 8,192 or
16,384 samples for other programs. The interpolation register budget remains
65,536 elements, and each shader's maximum batch still applies. Wire validation
admits 16,384 and rejects invalid capacities. The default remains 4,096.

Six fixture runs pass independently on CPU and NPU: three batch sizes, each with
textured-triangle and derivative tests. These include mixed affine, perspective
and flat interpolation, odd target dimensions, partial batches, scissor/mask
coverage and derivative helper pixels. Coverage is exact; the triangle oracle's
maximum byte error is one. The larger interpolation and derivative cases exercise
the enlarged graph shapes, rather than only the unchanged small paths.

The initial CPU controller incorrectly treated a single PowerShell pipeline
object as an array and reported missing evidence despite a passing executable.
That failed report and the corrected six-case qualification are retained.

On the NPU, both 8,192 and 16,384 reproduce all 65 selected expensive desktop draws
with exact original output hashes and delivery acknowledgments. These runs include
cold graph preparation and are correctness qualifications, not matched speed tests.

## Compiled-model sharing candidate

Worker v36 separately derives from v34. `NR_RENDER_MEMORY_CACHE_CAPACITY` accepts
64 or 512; the default remains 64. Sharing still requires the existing explicit
`NR_RENDER_MEMORY_CACHE=1` setting. Model identity, aliases, compilation properties,
independent mutable infer requests and owning-core lifetime rules are unchanged.
The cache retains its finite eviction and poisoned-failure behavior.

The CPU and NPU contracts each verify 2,368 values, four backend compilation calls,
two cache hits and three evictions. They test alias/property separation, distinct
request inputs, the original caller core's destruction, active requests surviving
eviction, invalid capacities and compile-failure poisoning. The larger capacity
also undergoes real recorded-stream qualification:

| Recording | Completed requests | Original reference hashes matched | Shared-model hits | Backend model loads | Evictions | Recoveries |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Older 256-request failure stream | 256 | 255 | 397 | 186 | 0 | 0 |
| Older 117-request failure stream | 117 | 116 | 153 | 103 | 0 | 0 |
| Live-v24 1,097-request stream | 1,097 | 1,096 | 873 | 257 | 0 | 0 |

All 1,470 requests complete on NPU. The three originally failing final requests
have no original successful output reference, so their success is not counted as
an original-hash match. Every available reference matches. Each replay uses one
child process with no recovery. These finite streams do not qualify indefinite
model retirement or the thirty-minute desktop target.

Fewer backend model loads do not establish lower total latency: hashing, model
setup, provider execution and shared resource contention can offset that saving.
The next gate compares batching alone with batching plus sharing on the same full
recording, then promotes only a measured improvement into native desktop testing.

## Reproduction and integrity

Both source patches apply independently to worker v34 in the
[provider recovery archive](../native-provider-recovery-v1/README.md). Build
identities, CPU/NPU receipts, failed controller evidence, replay records and source
scripts are preserved. All 99 collected text artifacts, totaling 3,881,212 bytes,
match hashes computed on Delta. See [report.json](report.json),
[raw-manifest.json](raw-manifest.json) and [source-manifest.json](source-manifest.json).
Pixels, compiled caches, binaries and credentials remain outside Git.
