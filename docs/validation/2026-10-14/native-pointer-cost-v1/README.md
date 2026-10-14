# Pointer recovery and rendering cost

Full desktop offload remains unfinished. This archive contains a failed native
pointer trial, qualified ordinary-desktop pointer recovery, and an offline NPU
optimization candidate. No Tahoma benchmark or live optimized-worker result is
included. The raw archive contains 203 remotely hash-verified text artifacts
(2,919,538 bytes); pixels, models and device caches remain excluded.

## Pointer behavior

Presenters 17 and 18 fail their ordinary Windows relative-mouse test. Presenter 18
shows that the cursor moved into the owned window, but the coalesced message lacked
the injected tag. Presenter 19 requests `MOUSEEVENTF_MOVE_NOCOALESCE` and passes the
relative move, mouse click, authenticated Unicode key, and rejected unauthenticated
character checks. Its eleven frame-reader checks also pass.

Helper 10 and an independent interactive watchdog pass boundary checks, relative
movement, normal release and forced owner-process termination. Confinement survives
termination, and the separate watchdog actually releases it and restores the
original position. This does not qualify failure of the watchdog itself.

Native live 22 receives the relative move and click, but its confinement guard
aborts on activation. The helper restores the topology exactly; ordinary foreground
restoration fails, and the complete input/color sequence does not pass. Pointer
confinement is released and native driver cleanup passes. The guard's original
failure log does not record the changed rectangle, so that trial alone does not
identify the new bounds.

Helper 11's ordinary-desktop qualification reproduces unrestricted pointer bounds
at two foreground transitions, reacquires confinement, completes the input test,
and again passes normal and forced-owner recovery. It preserves a different
application's confinement rather than replacing it. A subsequent native trial is
separate and is not qualified by this archive.

## Remaining device failure

After live 22 rolls back to the cloned topology, worker 27 reports
`ZE_RESULT_ERROR_DEVICE_LOST` on dispatch ticket 971: an ABI 3, 1024×768,
24-vertex draw. A fresh isolated worker successfully replays that exact recorded
request. No original successful pixel reference exists for it. This rules out a
deterministic failure in that isolated run; it does **not** establish the cause or
resolve the state/lifetime failure. The native controller removes its drivers and
restores its process permissions successfully.

## Cost and quad experiment

Analysis of the earlier successful live 21 recording joins all 1,465 render receipts
to their delivery acknowledgments and shader metadata. Aggregate measured times
are 451.186 seconds inside pipeline execution, including 358.680 seconds in draw
execution and 53.050 seconds in backend compilation. Worker time outside pipeline
execution totals 28.786 seconds; request round-trip time outside worker and queue
time totals 6.792 seconds. These are diagnostic boundaries, not a complete
wall-clock attribution or a matched performance benchmark. Recording and the
physical-output observer were active, and the fixture repainted every 15 seconds.

One sample-and-color-modulation shader accounts for 33.83% of aggregate pipeline
time. Thirteen expensive point-sampled draws are groups of three or four quads.
The isolated candidate feeds those quads in order through the existing affine
renderer, retaining NPU vertex, projection, pixel and blend work. All thirteen
outputs match the original pixels exactly on both CPU and NPU, twice per request.

| Component observation | Earlier native worker | Isolated quad candidate |
| --- | --- | --- |
| Provider calls per selected draw | 1,186 or 1,282 | 54 or 72 |
| Warm NPU draw time | Approximately 8–9 seconds | 142.554–248.311 ms; median 189.971 ms |
| Pixel comparison | Original recorded output | Exact match for all thirteen requests |

The contexts differ, so this table is a promising diagnostic comparison, not a
claimed end-to-end speedup. The candidate does not yet provide generalized geometry
fallback or a usable switch and is not installed in the native worker here.
Its patch is against the preserved worker 27 snapshot; manifests pin all inputs.

The next gates are native foreground recovery, integration with checked geometry
and explicit same-NPU fallback, matched worker comparisons, continued device-loss
investigation, responsive sustained desktop operation, application coverage and
Tahoma benefit.

[Report](report.json), [cost analysis](cost-analysis.json),
[raw manifest](raw-manifest.json), [source manifest](source-manifest.json),
[quad candidate patch](native-quad-v1.patch), [primary documentation](research.json).
