# Frame-batch live desktop trials on Delta (2026-10-20)

Two finite live trials ran the frame-batch UMD and worker under the real
Desktop Window Manager on Delta (Intel Panther Lake, 1024x768 logical desktop
through the native adapter). Both measured the same finite harness as the
earlier per-draw builds: `analyze-repeated-input-v2.py` counts the uniquely
verified colors of a 10 s, 30 Hz controlled animation (`nr_repeated_input.exe`)
that reached the display counter.

Evidence stays on Delta under `%LOCALAPPDATA%\npu-render\vm`:

| Trial | Chain | Build | Live transaction | Analysis | Result |
| --- | --- | --- | --- | --- | --- |
| 1 | registration v55 / desktop v76 / live v66 | native-tree-v6, package v24 (0.0.1324.0) | `native-live-v66` | `native-repeated-input-v13-analysis` | 2.1 verified updates/s |
| 2 | registration v56 / desktop v77 / live v67 | native-tree-v7, package v25 (0.0.1325.0) | `native-live-v67` | `native-repeated-input-v14-analysis` | 2.2 verified updates/s |

The per-draw baseline recorded earlier today (`native-repeated-input-v12-analysis`)
was 0.7 verified updates/s. The target remains 30.

Trial 1 completed its measurements but the live launcher failed a stale
"compiled turbo" audit gate inherited from the per-route chain (the frame
kernel blobs replace those kernels); the generator now audits the six frame
blob imports instead and trial 2 passed every gate (`ptl-native-live-v31`).

## What the UMD did under DWM

Every draw and clear DWM issued went through the batch admission. Over trial 1
(219 presents, 17,276 trace rows) the compositor's work split as:

| Present pattern | Count | Cost per present |
| --- | --- | --- |
| 2 batched draws, 1 flush | 46 | 31 ms (16 ms once uploads shrank) |
| 40 batched draws, 1 legacy draw (c71638aa), 6-7 flushes | ~22 | 560-660 ms (the animation frames) |
| 45 batched draws, 3 clears, 4 legacy draws, 8-9 flushes | 57 | 900-1100 ms (full compositions) |

Every animation frame of the controlled window therefore cost about 600 ms,
which is the 2.1-2.2 updates/s the analysis reports. The per-frame time is
dominated by draws that still take the per-draw route and by the flushes they
force, not by the batched kernel work:

| Fallback reason | Shader | Draws (trial 2) | Mean cost on the per-draw route |
| --- | --- | --- | --- |
| sampler state (mirror addressing) | e3ebf67c (224 instructions, loop) | 122 | 119 ms |
| span budget: t1 1024x1056 sampled through tiling arithmetic | c71638aa (acrylic-style backdrop, 1024x48 strip) | 89 | 256-420 ms |
| lowering: pixel opcode (`deriv_rtx`/`deriv_rty`) | e455f7a5, d305ca5e | 81 | 50-130 ms |
| not a compiled inventory program | e87ed647 | 12 | 305 ms |
| span budget: 318-348 vertices | d8e0b497 | 8 | 43 ms |

One c71638aa draw sits in every animation frame. Its texture t1 is sampled
through mirror/wrap arithmetic that the planner could not bound, so the whole
4.3 MB texture would have to travel in a 264 KB span.

## Upload volume and flush cost

Trial 1 uploaded the full 1024x768 primary with every flush (median 3.2 MB,
2.4 GB in total) because shared targets were treated as volatile and swap-chain
rotation reset the worker's resident copy each present. Trial 2 ran the
reworked UMD (dirty-region tracking, identities that rotate with the backing
handle, shared textures compared once per frame):

| Measure | Trial 1 | Trial 2 |
| --- | --- | --- |
| Flushes | 525 (mid-trial snapshot) | 996 |
| Upload bytes per flush, median | 3,237,304 | 4,608 |
| Flush wall time, median | 16 ms | 16 ms |
| Flush worker time, median | 3.1 ms | 2.5 ms |
| Flush reasons | not recorded | 453 read_region, 253 legacy_draw, 116 present, 86 write, 84 hazard |

The upload volume fell by three orders of magnitude but the median flush still
costs 16 ms of wall time against 2.5 ms in the worker. The remaining UMD-side
cost is the full-surface staging readback of shared textures for the change
comparison and of the primary for region copies, plus the write-back of the
dirty rectangles into VidMm through staging allocations. DWM's five small
region copies per frame each force a flush because they read pixels inside the
pending draw bounds.

## Kernel time

Across trial 2 the worker executed 2,989 kernel requests for 996 batches. The
heavy batch of an animation frame (6 commands over the window region) took 7
kernel requests and about 130 ms of kernel time: roughly 84 tiles of 128x128
pixels shaded at 0.2-0.4 us per pixel on six SHAVE engines plus the 5-7 ms
request floor. Even with every draw batched and every flush free, this frame
would take about 150 ms. Reaching 33 ms per frame needs the shaded pixel
count per frame to fall (DWM redraws several full-window layers per update)
and the per-request floor to shrink, in addition to the fallbacks above.

## Changes made from these trials

- UMD: resident targets with dirty-region uploads, rotation-following
  identities, once-per-frame shared texture comparison, region-aware flushes,
  flush reasons and detailed fallback reasons in the trace.
- Worker: partial uploads into resident copies, version-based reuse of shared
  surfaces, per-command batch listings.
- Planner: per-lane coordinate provenance (moves and constant clamps keep a
  varying's provenance) and interval evaluation of the pixel stage so
  coordinates computed from varyings get bounded texel windows instead of the
  whole texture. Validation against the trial 2 request capture is the next
  step.
- Chain: `make_trial_chain.py --generation N --capture` writes fresh
  transaction names and records every DWM request the worker receives; the
  retired compiled-turbo audit became a frame blob import audit.

## Limits

These are controlled-desktop measurements on one machine. They do not measure
Tahoma, GPU utilization or panel photons. CPU-side timings and request counts
describe the pipeline's cost; only the display-counter-verified animation
colors count as delivered updates.

## Offline validation of the interval planner (trial 2 capture)

Trial 2 recorded every packet DWM sent to the worker (1,335 requests: 1,002
batch packets and 333 per-draw packets). `nr_frame_batch_replay_convert`
passes recorded batches through unchanged and `replay_frame_batches.py` replays
them in order through one worker built from the interval planner
(`native-tree-v9`, `frame-replay-v5-124418\replay-kernel-2` on Delta):

| Measure | Value |
| --- | --- |
| Batches replayed | 1,002 recorded + 116 converted, 217 per-draw packets |
| Converted and per-draw results exact against the recorded responses | 333 of 333 |
| Kernel escapes (taps outside a texel window) | 0 |
| Acrylic-style shader c71638aa commands on the kernel route | 103 of 103, 128-pixel tiles |
| Commands still falling back at fit time | 107 (318-348 vertex glyph draws, span budget) |

Zero escapes across every batch means no tap ever left the windows the
interval evaluation planned, so the kernel's output for those commands equals
the output it would produce with the whole texture resident. The worker can
also be forced onto the qualified per-draw route for every command
(`NR_RENDER_FRAME_BATCH_FORCE_FALLBACK=1`) and the replay tool compares two
such runs rectangle by rectangle; that oracle needs the per-draw route
selectors that batched draws now carry (they were zero in trial 2's capture).

Per-command listings from the replay show what the controlled animation makes
DWM draw: each update is a five-pass redraw of a 960x768 region (two solid
passes with 95b1c4fb, two textured passes with 595296ba, one 960x48 strip),
about 3.7 million shaded pixels per frame, plus the acrylic strip. At the
kernel's 0.2-0.4 us per pixel that is 130 ms of NPU time per frame before any
flush or transport cost.

Batched clears are host fills of the worker's resident target (the span
background), not NPU kernel work; the driver still counts them as NPU
operations because the registered probe expects eight NPU executions for six
clears and two draws. That accounting needs a dedicated fill command or a new
probe contract.

## Trial 3 and the kernel optimization pass

Trial 3 (`native-live-v68`, build native-tree-v9 with the first interval
planner, package v26) passed every chain gate. Its full analysis aborted
because one controlled mouse event (sequence 9) had no matching displayed
counter; `native-repeated-input-v15-partial-analysis` records that event as
skipped and reports 1.9 verified updates/s for the animation (21 unique
submissions, 19 displayed in the window). The acrylic strip still fell back
in this trial: the interval evaluation bounded its second texture to a 771x459
window for the whole 1024x48 strip, too large for a span once the floor and
fraction arithmetic lost the correlation between a coordinate and its
integer part. Affine forms in the pixel position (build native-tree-v10 and
later) keep that correlation.

The emitted row shaders were then changed to compute their interpolation
reciprocals once per row (the edge weights sum to a row constant and every 2D
draw has w = 1) and the sampler loads a single texel when the other bilinear
weights are zero. Blobs 0 and 1 overflowed the 24,496-byte code window by 128
and 32 bytes after this change, so the programs are partitioned into seven
blobs ({0-1}, {2-3}, {4-12}, {13-64}, {65-115}, {116-120}, {121+}). All seven
built with one assembler-requested NOP at most and qualified exactly on the
NPU against their fixtures (`frame-blob{0..6}-qualify-130815`, deployed as
`frame-blobs-v3`):

| Blob | Programs | Code bytes | Fixtures | Mismatches |
| --- | --- | --- | --- | --- |
| 0 | 5 | 18,208 | 23 | 0 |
| 1 | 8 | 24,448 | 35 | 0 |
| 2 | 3 | 20,480 | 15 | 0 |
| 3 | 2 | 21,408 | 11 | 0 |
| 4 | 1 | 22,576 | 7 | 0 |
| 5 | 1 | 22,768 | 7 | 0 |
| 6 | 1 | 22,544 | 7 | 0 |

## Affine arithmetic, adaptive tiles and the seam limit

Interval evaluation alone widened the acrylic strip's second texture window
to 772x289 texels per 128-pixel tile because it lost the correlation between
a coordinate and its integer part in the shader's wrap arithmetic. The
planner now evaluates the pixel stage in affine arithmetic over noise
symbols (the pixel position plus one symbol per non-linear result); linear
combinations of correlated values stay exact, so the same tile needs 260x99
texels and fits a span. Replaying the trial 2 capture through the v12 worker
confirmed zero escapes, and a tolerance replay against the per-draw route
found every converted batch within 1 LSB on at most 0.013% of pixels
(`frame-replay-v8-132229\replay-reference`).

The strip under the taskbar (y 720-768) still cannot batch: its texture
coordinates cross a wrap seam in both axes inside the strip, and a pixel whose
bilinear taps straddle both seams needs texels from both ends of a 256-texel
period in u and v. One rectangular window per texture must then hold the whole
256x256 period (262 KB), which exceeds a 264 KB span once the tile and headers
are added. Bisecting tiles down to 8 pixels (now done by both the driver's
admission and the worker) admits every other seam case but not this corner.
Two windows per texture in the kernel, or a larger carrier row, would remove
the limit; until then that draw stays on the per-draw route at ~400 ms.

## Trial 4 (build v13, seven blobs, affine planner, adaptive tiles)

`native-live-v69` (registration v58, package v27 / 0.0.1327.0, `frame-blobs-v3`)
passed every chain gate and `native-repeated-input-v15-analysis` reports 1.9
verified updates/s (21 unique submissions, 19 displayed in the window). The
animation frames still cost 600-640 ms each: every one of them contains the
taskbar backdrop draw (c71638aa over 1024x48 at y 720) whose per-tile window
straddles a wrap seam in both axes, so it stays on the per-draw route (329
per-draw executions, 83 s of the 12-minute trial). Over the whole trial the
driver flushed 1,053 batches (median worker time 1.8 ms, median upload 1.2 KB)
and the kernel ran 3,433 requests for 24.1 s; the worker's timing split shows
the NPU request itself, not host copies, carries that time (replay: 22.0 s of
inference against 0.45 s of copies for 3,366 requests).

Where the frame time goes, measured across the four trials:

| Component of a 1024x768 animation frame | Measured | What removes it |
| --- | --- | --- |
| c71638aa backdrop strip on the per-draw route | ~400-600 ms | two texel windows per texture in the kernel, or a larger carrier row |
| Five full-region DWM passes (3.7 Mpx) on the kernel | ~130 ms (6-7 requests) | a vectorised row kernel (several pixels per vector), fewer blobs per frame |
| Flushes forced by DWM's region copies | 5 per frame, ~2-16 ms each | serve small readbacks from the worker's resident copy |
| Per-request NPU floor | ~1 ms light, 18 ms for twelve full tiles | more rows per carrier request |

The per-draw baseline was 0.7 updates/s; the batched renderer reaches 1.9-2.2
on this harness. Reaching 30 needs the strip batched and roughly a five-fold
reduction of kernel time per frame; the measurements above bound each term.
