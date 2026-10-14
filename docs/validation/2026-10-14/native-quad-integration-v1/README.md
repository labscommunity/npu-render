# Native pointer recovery and matched quad rendering

Full desktop offload remains unfinished. The finite native pointer trial passes,
and an integrated NPU draw optimization passes correctness and matched component
timing checks. Tahoma and responsive desktop operation are not qualified here.

## Native interaction

Live v23 completes from 23:08:31 to 23:20:15 UTC on October 14. Windows DWM issues
1,559 acknowledged NPU operations, producing 31 native frames with 17 distinct
pixel hashes. The physical presenter submits 28 frames, each matching its source.
An independent physical-output observer verifies blue, mouse-triggered green,
and keyboard-triggered red. Relative mouse movement is authenticated.

The pointer guard reacquires confinement after two foreground transitions.
Exact original topology, ordinary window placement, cursor position, foreground
window, and unconfined cursor state are restored. Native and cursor cleanup pass.
This extends the [ordinary pointer qualification](../native-pointer-cost-v1/README.md)
to the finite NPU-primary desktop trial. Secure desktop, sleep/wake, unrestricted
application interaction, and all failure combinations remain unqualified.

Worker v27 is used in this trial. CPU builds and CPU contract checks ran during
the diagnostic, and private request recording and screen diagnostics were active.
Its elapsed times are not a matched desktop performance measurement.

## Integrated rendering change

Worker v28 recognizes eligible point-sampled groups of six vertices using shader
semantics and state checks. It preflights every quad on the selected backend
before producing any pixels, then renders accepted quads in order through the
qualified affine renderer. Unsupported geometry explicitly uses the ordinary NPU
path from the original background. Provider failures propagate. The experiment
requires `NR_RENDER_QUAD_BATCH=1`; the default path remains unchanged.

CPU and NPU contracts pass 16 positive cases, including overlap, reversed order,
channel exchange, source-over/replace, alpha endpoints, and clipping. They compare
49,152 color components with the generic CPU renderer. Sixteen unsupported
geometry cases reject before rasterization, and twenty static cases reject.
A separate real-NPU pipeline test verifies acceptance, explicit same-NPU fallback,
and static rejection through three exact-output cases (9,216 components).
Thirteen captured expensive draws also match their original pixel hashes on both
CPU and NPU, twice per request. Captured pixels remain private on Delta.

## Matched component comparison

The order is baseline A1, candidate B1, candidate B2, baseline A2. Each phase uses a
fresh worker and the same thirteen recorded requests, runtime, model cache, shared
queue/Core policy, and retained pipeline setting. No live desktop experiment or
other NPU workload overlaps these measurements. Every one of the 52 outputs
matches its original pixels and receives a delivery acknowledgment.

| Phase | Compile plus draw total | Worker total | Round-trip total | Provider requests |
| --- | ---: | ---: | ---: | ---: |
| A1, worker v27 | 121,425.994 ms | 121,468 ms | 121,735 ms | 15,898 |
| B1, worker v28 | 3,069.181 ms | 3,232 ms | 3,482 ms | 880 |
| B2, worker v28 | 3,130.069 ms | 3,217 ms | 3,516 ms | 880 |
| A2, worker v27 | 120,462.192 ms | 120,579 ms | 120,768 ms | 15,898 |

Mean compile-plus-draw time decreases 97.44% (39.02×), and mean round-trip time
decreases 97.11% (34.65×). This applies to these thirteen expensive operations,
not a complete desktop frame or an application benchmark. Candidate median draw
times are still 221.898 and 234.547 ms, above the desktop responsiveness target.

## Evidence and remaining work

The archive contains 174 text artifacts totaling 3,655,434 bytes, verified against
hashes generated on Delta. Source v28 is a patch against the preserved v27 source.
Build receipts pin source, dependencies and binaries. See [report.json](report.json),
[raw-manifest.json](raw-manifest.json), and the
[matched analysis](raw/native-quad-abba-v1-analysis/report.json).

The complete 972-operation rollback stream, registered-driver promotion and live
v24 are separate trials still in progress when this archive was created. The
earlier topology-rollback device loss is not declared resolved. Application
coverage, residual GPU work, sustained responsiveness and Tahoma benefit remain
open gates.
