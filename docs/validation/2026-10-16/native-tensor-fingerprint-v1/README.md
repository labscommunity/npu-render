# Individual NPU tensor reuse diagnostic

The instrumented worker passes 106 selected draws and all 972 retained draws,
matching every worker-v19 image. Program, complex-shader and rectangle kernels
are unchanged. The full replay fingerprints 7,531 actual NPU calls: 6,647 program,
626 complex and 258 rectangle calls. It finds 3,368 distinct input fingerprints
and no differing output fingerprint for a repeated input. Every call still runs
on NPU; no result cache exists.

| Simulated cache | Hits | Observed NPU wait on hypothetical hits |
| --- | --- | --- |
| 64 MiB | 644 | 1.071 s |
| 128 MiB | 1,004 | 1.838 s |
| 256 MiB | 1,193 | 2.607 s |

Each entry accounts for a full 1.5 MiB input and output. These are fingerprint-only
opportunity estimates, not saved rendering time. They omit exact-byte collision
checks, allocation, copying and lookup. The diagnostic itself spends 186.093 s
hashing versus 35.266 s waiting for the measured NPU calls. It changes timing and
must not be compared with the uninstrumented worker as a performance result.
Geometry and other provider calls outside these three components are not covered.

Two SHA-256 known-answer checks qualify the fingerprint helper. Worker shape,
NPU execution, model hashes, request correlation, image hashes and existing
recovery contracts remain checked. The [manifest](artifact-sha256.json) retains
69 artifacts, including all worker sources, build identity, selected/full logs
and qualification. No desktop or Tahoma test ran in this diagnostic.
