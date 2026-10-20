# Combined renderer desktop: passes finite checks, 0.8 updates/s

Worker24 passes the separate registered Direct3D check, native-live-v60 and
native-desktop-admission-v70. The original uninstrumented driver package19 is
used. The 550-second presentation run delivers 1,694 NPU child receipts, with
one provider generation and no provider fault or retry. There are 107 matching
physical submissions, exact backbuffer copies and 92 distinct source hashes.

The run exercises 154 resident clipping draws, 52 routed rectangles, balanced
geometry, two texture-specialization draws and the existing shader routes. The
ordinary shared default-adapter fixture completes eight NPU operations. All forty
controlled input responses are physically observed. Pointer state, ordinary
windows, primary topology and driver state restore successfully; memory integrity
remains enabled. These finite checks do not establish broad application support.

Responsiveness remains inadequate:

| Metric | Result |
| --- | ---: |
| Scheduled animation | 300 colors at 30 Hz over ten seconds |
| Producer skips | 0 |
| Verified updates during schedule | 8 (0.8/s) |
| Verified updates including drain | 10 |
| Warm mouse sent-to-display median / p95 | 89.064 / 1,725.807 ms |
| Warm keyboard sent-to-display median / p95 | 83.286 / 96.931 ms |

There are nineteen warm observations per input kind; the nearest-rank p95 is
therefore the maximum. One mouse event dominates that tail. The
[per-input attribution](analysis/repeated-rendering.json) finds 1,443.783 ms
between its receipt and publication, including 820.157 ms of child rendering,
248.661 ms in the remaining serialized worker stages and 374.965 ms outside the
serialized parent. This interval is narrower than total sent-to-display latency.
It includes a broad scene redraw; a single shader does not explain the stall.

During animation, child rendering occupies 6,353.803 ms, with 548.129 ms before
child connection, 451.365 ms reading the child packet, 1.538 ms before child
execution, 477.481 ms after child completion and 2,167.683 ms outside serialized
parent intervals. Eight frames are published and submitted. The earlier passing
worker delivered seven updates in its separate ten-second run; one additional
update here does not establish a sustained frame-rate improvement. The GPU
control delivered 29.9/s through the same bridge.

The 163-file manifest retains exact trial sources, reports, traces, timing and
physical-observation analyses. Full desktop offload, normal responsiveness,
application coverage, iGPU relief and Tahoma benefit remain unqualified.
