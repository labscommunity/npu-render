# Bounded desktop delivery and a custom Panther Lake kernel

The corrected short native trial passes continuous-delivery checks. A separately
isolated, locally compiled SHAVE identity kernel also executes correctly on
Delta's Panther Lake NPU. Neither result qualifies usable full desktop offload,
application migration, or Tahoma benefit.

## Desktop delivery and recovery

Live v35 acknowledges 1,248 NPU operations and submits 43 exact matching frames to
the physical presenter. First-to-last delivery spans 518.604 seconds; the last
frame is 5.076 seconds old at the end. The independent DXGI observer completes
successfully and confirms owned blue, mouse-triggered green, and keyboard-triggered
red output. Three physical observations satisfy the final freshness window.
Topology, ordinary windows, pointer confinement and process permissions restore.

The validator's 60-second liveness bound detects frozen delivery; it is not the
100-ms responsiveness target. The sparse animation workload is not a maximum
frame-rate benchmark. The presenter issues 86 GPU copy commands, including 43
readbacks, and the measurement observer issues 254 additional copies. This is
not evidence of zero GPU work. General default application device creation still
fails without admission; its negative observations are retained.

V33 remains a failed trial: its observer started later than the presenter but ran
for the same duration, outliving the presenter. V35 shortens observation by 15
seconds while retaining the final freshness checks. No old result was rewritten.
The v34 fault test deliberately terminates its owned relay after three verified
publications. Its controller detects the failure and restores topology, windows,
pointer and permissions in 25.749 seconds, leaving no native child processes.
The v34 trial fails as expected; the independent fault qualification passes.

## Custom kernel proof

The publicly downloadable, Lenovo-signed driver package was extracted solely to
obtain MoviTools. Its old driver was not installed; installed NPU driver
32.0.100.5540 remains unchanged. Proprietary tools, graph blobs, device caches and
binary kernels are excluded from this archive. Package integrity and compiler
file hashes are retained. Redistribution rights remain unresolved.

The available compiler, assembler and linker accept `5000xx`. Acceptance alone
was insufficient: the generic upstream npunlock inspector rejects this Panther
Lake graph. Intel's pinned NPU50XX range/invocation definitions, firmware entry
ABI, MemRefData layout and ELF no-file section types informed a separate, narrow
bounds-checked parser. Primary source URLs and hashes are retained under
`sources/ptl-abi-research-v1/sources.json.txt`.

The original graph has six eight-element FP16 software invocations: two
partitions replicated across three tiles. A 288-byte compiled identity kernel
replaces only the common code extent, retaining all other bytes, tensor spans,
relocations, scheduling and code-size metadata. Seventeen offline contract cases
cover the accepted layout and rejection of malformed headers, descriptors,
relocations, aliases and oversized replacements.

An isolated child imports three graphs in sequence: original add-one, custom
identity, original add-one again. Each phase runs eight changing 16-element inputs
and matches all 128 components exactly. Execution devices report NPU only. The
custom output differs from the original add-one semantics and matches the input
hash in every case; the final original phase also passes. No desktop settings or
installed drivers change during this test.

The recorded `time.monotonic()` durations are quantized at roughly 15 ms, including
zeros. They are not usable latency measurements. Subsequent timing must use
QueryPerformanceCounter / `perf_counter_ns`. This identity proof does not validate
texture-wide addressing, floating-point arithmetic, shader translation, or a
rendering speedup. Those are the next kernel gates.

## Evidence

`raw-manifest.json` records 327 remote SHA-256-verified files (9,584,482 bytes).
`source-manifest.json` covers the additional local harness, kernel and source
provenance. Original failed preparatory runs and successful retries are retained.
`report.json` is derived from assertions over these raw records. Source snapshots
have `.txt` suffixes to preserve their experimental status.
