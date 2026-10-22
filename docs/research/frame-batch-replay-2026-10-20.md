# Frame batch replay of the recorded desktop capture (2026-10-20)

Capture: `vm\native-desktop-admission-v33\bootstrap\requests` on Delta, 971
recorded per-draw NPU requests from the registered DWM session (1024x768 and
2880x1800 targets, color clears, textured draws up to 510 vertices).

Tools: `nr_frame_batch_replay_convert` turns the capture into frame batch
packets (per-draw: one batch per request; chained: consecutive draws on one
target whose background equals the previous result join one batch), and
`tools/delta/replay_frame_batches.py` replays them on one isolated worker
(`nr_registered_local_indexed_worker`, build native-tree-v4, kernel blobs
`vm\frame-blobs-v1\deploy`) and compares the target pixels with the recorded
per-draw route replayed on the same worker (`--reference`, tolerance: channel
difference <= 2 and <= 1% differing pixels). Draws whose shader pair is not a
compiled inventory program, or that the kernel cannot plan, replay as the
recorded per-draw packet ("legacy").

Evidence: `vm\frame-replay-v2-104029\replay-per-draw\report.json`,
`vm\frame-replay-v2-104029\replay-chained\report.json`.

| mode | entries | batches | legacy | within tolerance | exact hashes | fallback commands | kernel requests | kernel ms | host ms |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| per-draw | 971 | 899 | 72 | 899 / 899 | 922 / 971 | 33 (span budget) | 1184 | 5674 | 21740 |
| chained | 423 | 351 | 72 | 351 / 351 | 393 / 423 | 33 (span budget) | 820 | 5389 | 21117 |

Legacy reasons: 66 draws use pixel opcodes the kernel does not lower
(shaders e3ebf67c, d305ca5e, e455f7a5), 6 use non-clamp texture addressing.
The 33 fallbacks inside batches are draws that minify a texture so strongly
that even 32x32 tiles cannot hold their texel windows in one span; the worker
runs those through the recorded per-draw route in order.

Kernel time per request averages 4.8 ms (12 tiles per request); host time
includes the uploads every self-contained batch carries (a full target
background plus textures, up to 24 MB), which the resident-resource protocol
removes in the driver path. These are worker-side timings, not display frame
rates.
