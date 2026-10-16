# Three-texture worker in the live Windows desktop

Native live v39 passes with worker v4. It delivers 52 matching physical-output
submissions spanning 537.906 seconds, with 34 distinct source pixel hashes and
1,140 acknowledged NPU operations. One provider generation completes with zero
faults or retries. Eighteen four-sample draws, six solid draws and one three-texture
draw use the custom kernels. The three-texture admission occurs late in the trial;
an intermediate inspection with zero admissions was not its final result.

The controlled blue/green/red observations, mouse and keyboard receipts, relative
pointer motion, confinement and two reacquisitions pass. The relay terminates
successfully; the independent freshness validator verifies continued delivery.
Topology, ordinary windows and cursor state are restored. The freshness bound is
60 seconds and does not establish usability.

The first observed green follows the mouse receipt by 11.5877 seconds; red follows
the key receipt by 62.2 ms. These two events include observation cadence and are
not p95 latency, panel photon timing or a matched comparison with earlier trials.
Responsiveness still fails the project target. The derivation is retained in
[controlled-response-analysis.json](controlled-response-analysis.json).

The presenter issues 104 GPU copies, including 52 validation readbacks; the
physical-output observer adds 142 measurement copies. These are command counts,
not GPU time, bandwidth or power. Physical scanout remains on the Intel display
hardware. Unleased default Direct3D creation still fails with `887a0004` for both
feature lists. Application migration, Tahoma benefit, this renderer's thirty-minute
stability and a usable full-offload switch remain unqualified.

A read-only attribution of the completed worker-v4 replay identifies 28,835
provider calls across 972 operations and 151.418 seconds of summed draw time.
The largest shader group (`d8e0b497…`) accounts for 41 requests, 4,996 calls and
22.293 seconds. This is attribution within one diagnostic replay, not a performance
comparison. It motivates reducing common geometry and dispatch costs.

The artifact manifest preserves raw evidence and source snapshots. Model blobs,
executables and desktop pixels are excluded. The legacy `desktop_offloaded` and
`input_forwarding_tested` fields remain false; the explicit native composition,
physical delivery and `normal_windows_input_tested` evidence describe this limited
qualification. It does not establish full desktop offload.
