# Reusing compiled NPU programs

An opt-in cache of 64 compiled programs completed the full 1,438-request desktop
trace, followed by a disabled-cache control using the same executable. Both
runs produced identical pixels and all rendering receipts. The enabled run
took **530.356 seconds**, versus **577.900 seconds** disabled: an observed
**8.23% reduction** in this pair. This experiment uses the point-reentry renderer
without compact coverage; the two optimizations' gains cannot be added.

| Measurement | Cache enabled | Cache disabled |
| --- | ---: | ---: |
| Total replay time | 530.356 s | 577.900 s |
| Summed setup plus draw | 422.475 s | 468.499 s |
| Summed draw time | 285.677 s | 283.942 s |
| Calls requesting a compiled program | 1,730 | 1,730 |
| Programs reused from memory | 1,173 | 0 |
| Actual OpenVINO compile/load calls | 557 | 1,730 |
| Disk cache hits | 557 | 1,730 |
| Time inside the provider setup helper | 65.293 s | 108.312 s |
| Time inside OpenVINO compile/load | 26.750 s | 78.522 s |

Every actual compile/load was a disk-cache hit. This measures avoiding program
reloads, not avoiding 1,173 cold compilations. Both runs made 57,221 rendering
calls. Setup plus draw fell 9.82%; drawing itself did not improve. The setup
helper includes identity hashing, cache lookup, provider configuration and
compile/load. Its inner compile/load timer is a subset, not additional time.
All these timers use a monotonic high-resolution clock. Per-request validation
checks timer nesting and the identities between setup, memory-hit and backend-call
counts. Its comparison helper passed 59 boundary cases.

The cache key includes canonical graph content and constants, exact external
port aliases, backend, runtime, explicit compilation properties and disk-cache
directory. It retains the creator's Core context, reuses only compiled programs,
and lets each consumer create its own mutable inference request. A bounded LRU
evicts cached references while active requests retain their ownership. Unknown
property types are rejected. Compile failures poison the cache; the filesystem
worker also poisons it after request failure. The experimental wrapper admits
only explicit NPU selection on the pinned 2026.4.1 runtime. It assumes the known
renderer factories own all Core configuration; it is not a general cache for
arbitrary externally configured Core objects.

The first Windows build failed because the worker's new provider header lacked
an explicit OpenVINO target dependency. V2 adds that dependency and preserves
the failed V1 build. V2 passes all 29 CPU contracts and four NPU test stages:
cache ownership, raster, uniform draw and textured triangles. The cache ownership
test verifies 2,368 values across independent changing input buffers, destruction
of the original Core, eviction with active requests, and injected compile failure.
It records two memory hits and two evictions. This standalone test supplies its
own LATENCY/ACCURACY properties; the shared-queue environment option applies to
the integrated renderer tests through their wrapper, not to the standalone test's
direct compilation calls.

Local macOS CPU checks independently pass the identity and ownership contracts
with OpenVINO 2026.1 and an OpenSSL SHA-256 test adapter. Windows keeps its BCrypt
implementation. These local results are identified separately from NPU evidence.

This is one enabled-then-disabled pair. Temperature, clocks and background work
were not fixed; this is not a confidence interval or a live desktop measurement.
The enabled-run memory sampler started after the first 384 requests and therefore
cannot establish a full-run peak. Within its interval it observed a maximum
working set of 1,325,416,448 bytes and private memory of 379,768,832 bytes. The
cache evicted 493 entries. An entry-count limit is not a byte allocation limit.

Twenty-four remote artifacts were checked against SHA-256 and size receipts.
Their original bytes are losslessly compressed under `raw/`; source patches,
build scripts, executed harnesses and local checks are retained alongside them.
Source hashes match the clean Windows build. The full replay reports retain
every request digest and receipt. Physical display migration, live desktop
responsiveness and Tahoma performance were not tested. Production code remains
unchanged; this is an isolated experiment.
