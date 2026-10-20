# Unbounded reuse opportunity in the fingerprint trace

The retained instrumented replay contains 7,531 profiled program, complex-shader
and rectangle calls, with 3,368 distinct inputs and 4,163 later duplicates.
Measured NPU waits on those duplicates total 16,889.336 ms out of 35,265.993 ms.
This is an unbounded-cache opportunity count, **not achieved saved time**.

The [original bounded-cache analysis](../../2026-10-16/native-tensor-fingerprint-v1/README.md)
retains only 1.07–2.61 seconds of those waits in hypothetical 64–256 MiB caches
that store complete input/output tensors. A compressed or semantic representation
could retain more entries, but its correctness, lookup, collision verification,
storage and transfer costs have not been measured. No cache is implemented.
The instrumented replay also incurs expensive hashing; its waits cannot be
subtracted from a different uninstrumented rendering run.

The source and [report](report.json) are local analyses of the committed trace,
whose SHA-256 is pinned in the report. The [manifest](artifact-sha256.json)
preserves both files. This supplies no desktop or Tahoma performance result.
