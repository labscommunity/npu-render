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

## Two texel windows, kernel fills and copies, derivatives (builds v15-v20, blobs v4-v10)

The seam limit of trial 4 is gone: `nr_sample` serves a tap from either of two
rectangular windows per texture (`textures2` in the command record, window B in
`nr_command_state`), the planner's affine evaluation yields two-case values at
`frc`/`floor` crossings and `texel_windows` returns up to two rectangles, and the
executor bisects a 128-pixel tile when its first command does not fit a span.
Replaying the trial-2 capture (1,335 requests) through the v17 worker and the
nine v6 blobs put all 108 c71638aa draws on the kernel (57 at 32-pixel tiles,
37 at 64, 14 at 128) with zero window escapes and 176 tile splits; 19 glyph
draws of 318-348 vertices remain on the per-draw route (span budget).

The replay attributes kernel time per shader and tile (single-shader batches):

| Shader, tile | Batches | Kernel ms | Requests | Covered px | ms per request | us per covered px |
| --- | --- | --- | --- | --- | --- | --- |
| c71638aa, 32 | 114 | 3,238 | 342 | 2.80 M | 9.5 | 1.16 |
| c71638aa, 64 | 74 | 2,347 | 84 | 2.05 M | 27.9 | 1.14 |
| 95b1c4fb, 128 | 32 | 385 | 44 | 6.94 M | 8.8 | 0.055 |
| d8e0b497, 128 | 130 | 295 | 95 | 155 k | 3.1 | 1.9 |
| b7f4e6d0, 128 | 60 | 82 | 90 | 45 k | 0.9 | 1.8 |

Total kernel time rose from 22.5 s (seven blobs, strip on the per-draw route) to
36.4 s because the strip now runs on the kernel (about 6 s of it) and the
derivative shaders d305ca5e/e455f7a5 joined it; the per-draw route's time for
those draws left the ledger. For the animation frame the strip draw costs
28-32 ms of kernel time against 400-600 ms on the per-draw route.

Other changes validated by the same replays: clears execute as kernel fill
commands (kind 1, ~2 ms per request) and region copies as kind-2 commands
between resident resources; map hooks flush before a read or write of a batched
resource; the derivative opcodes (`deriv_rtx`/`deriv_rty`) lower to per-row
gradients of the interpolated varyings with the quotient rule under perspective;
the uncaptured taskbar glyph pair (e87ed647 over vs 54e59055) and a 64x64 pass
(0324b34c) were recovered from the trial-2 request log and compiled into the
inventory (50 native programs); the deployed blob manifest lists each blob's
programs and the worker disables a blob whose list differs from its inventory;
a failing kernel tile (status or request error) restores the touched region and
sends the run through the per-draw route instead of failing the batch.

## Device float semantics: the acrylic alpha (batch 46)

The tolerance replay flags one batch: the taskbar strip drawn over a transparent
target. The kernel wrote alpha 255 where the recorded per-draw route wrote 250
(RGB identical, all 276,480 covered pixels). The SM4 scalar reference, run on the
real vertices, constants and textures of that pixel, gives alpha 0.98086 (250):
the kernel was wrong. Causes found and fixed, each with a host check:

1. `nr_v4_min`/`nr_v4_max` returned NaN when the first operand was NaN; SM4
   returns the other operand. The acrylic shader divides the noise texture's
   colour by its alpha, which is zero in this frame. Host trials with transparent
   textures went from 34 of 64 cases more than one byte off to none.
2. The SHAVE divider does not give inf/NaN for x/0 and 0/0 (the host did, so
   fixtures with random textures could not show it). `nr_v4_div` now forces the
   IEEE results explicitly, out of line (inline copies pushed blobs 6-8 past the
   code window).
3. Comparisons are now explicitly ordered (a NaN operand gives false for lt/ge/eq
   and true for ne) so a device compare with other unordered behaviour cannot
   change a select.

The host test gained 32 transparent-texture trials per program and a fixture
("transparent-textures-<sha>") per textured program; `strip_check` in the
session scratchpad builds fixtures from the real strip tiles of the capture so
the device result for this exact draw is checked by `qualify_frame_kernel.py`.

## Transparent source-over draws

The animation frame batch (eleven commands over the 1024x768 target and its
1024x48 taskbar strip target) contains two solid 95b1c4fb draws per target whose
vertex colour is (0, 0, 0, 0) under source-over blending: the planner proves the
pixel stage writes exactly zero (three zero vertex values interpolate to zero
without a drift guard) and the executor records such a draw as a no-op that
touched no device. The replay skips 185 of 565 such draws and saves 7% of kernel
requests. The two "full-region" source-over passes are not no-ops: they are
one-pixel anti-aliasing rings (vertices at -0.5 and 32767.5) whose covered area is
small because the rasteriser computes exact per-row spans.

## Trial 6 (build v20, ten programs more, blobs v10): 1.9 updates/s

`native-live-v72` (registration v61, package v30 / 0.0.1330.0, `frame-blobs-v10`)
passed every chain gate; `native-repeated-input-v16-analysis` reports 1.9 verified
updates/s (21 unique submissions, 19 displayed in the window), unchanged from
trial 4 although the strip, the derivative shaders and the glyph pair now run on
the kernel. The archived trace explains the frame time:

| Flush reason | Flushes | Wall ms (sum) |
| --- | --- | --- |
| hazard (a draw samples a texture rendered earlier in the batch) | 599 | 43,813 |
| present | 121 | 8,802 |
| legacy_draw (per-draw route) | 138 | 6,777 |
| write / read / limit | 73 | 5,282 |

931 flushes carried 1.46 GB of uploads and 601 MB of writebacks; the kernel ran
4,287 requests for 32.0 s, and 139 per-draw executions took 29.7 s: 112 of them are
the blur pass e3ebf67c (two loops with `breakc`, 18 samples, mirror addressing)
drawn into 128x64 and 192x64 noise targets every frame, the rest derivative draws
whose point-sampled 2880x1800 source cannot be bounded to a span window. Each
animation submission waits about 520 ms.

Two changes follow from this trace and ship in build v24 / blobs v14 (the v23
worker was withdrawn by its own replay: a loop-range test in the planner's
evaluator was true outside loops, so no temporary was ever written, 532 draws
"proved" transparent and 670 recorded batches answered missing; the host suite
now cross-checks every transparency proof against the host kernel on the quad
fixture, and `frame_batch_planner.hpp` keeps the fix):

1. The driver no longer flushes at an intra-batch hazard. The worker executes the
   batch in order and fetches texel windows from its resident copy after the
   earlier runs have applied, so a draw that samples a texture rendered earlier in
   the same batch reads the right pixels; the executor already flushes its own
   kernel run when a draw samples that run's target. `NR_RENDER_FRAME_BATCH_HAZARD_FLUSH=1`
   restores the flush. Correctness is checked by replaying the next trial's capture
   against the per-draw route.
2. The kernel runs SM4 loops (`loop`/`breakc`/`endloop`, bounded by the reference
   interpreter's budget of 16 iterations and reported through the escape counter)
   and mirror addressing per axis (bit 17/18 of the texture binding; the planner
   folds mirrored coordinate spans onto one period and takes the whole texture when
   a span crosses a fold). The blur pass lowers natively and matches the reference
   on the host (36 of 36 comparable cases exact; the host suite now compares every
   program with mirror samplers and coordinates in [-0.5, 1.5]), but its 224
   instruction row function compiles to 27,472 B with the runtime, 3 KB over the
   24,496 B code window (the runtime alone is about 18.5 KB), so blob 9 is not
   deployed and e3ebf67c stays on the per-draw route: the worker reports
   "kernel blob 9 unavailable" for it. Fitting it needs a smaller runtime or a
   carrier with a larger code window.

Replaying the trial-2 capture through the v24 worker and blobs v14: 315 batches
exact, 332 within tolerance, zero window escapes, 185 no-op draws, 122 blur draws
on the per-draw route inside their batches ("kernel blob 9 unavailable") and 19
glyph draws over the span budget; the one batch beyond tolerance is the strip.

The acrylic strip's alpha (255 against the per-draw route's 250) is still open,
but it is not the kernel code: the worker's spans for that batch, dumped with
`NR_RENDER_FRAME_SPAN_DUMP`, are byte-identical to the fixture spans, and the
qualification tool gets 250 from the same deployed blob for them alone, in full
twelve-row requests, with stale bytes past the declared prefix, and after other
blobs ran in the same process. Input delivery is ruled out too: with build v29 the worker hands the plugin a
fresh host tensor per request (what the qualification tool does), with whole-row
copies, and with the output tensor cleared before each request, and the kernel
(12 requests, no fallback) still returns alpha 255. What is left between the two
processes is the NPU runtime context itself: the worker holds nine frame blobs and
ten per-draw-route models, the tool one. Loading and running all of those models
in the qualification tool before the strip spans made the device answer status 3
(program signature mismatch) for every span: a kernel other than blob 6's ran. The
NPU runtime can execute the wrong kernel image when several patched carriers have
lived in one process, and the worker had no way to notice. From bundle v17 every
blob writes its identity (the first 32 bits of its header's sha256) into output
byte 24, the deploy manifest carries it, and the worker treats an unexpected
identity as a failed run (restored and sent through the per-draw route), so a
wrong kernel can no longer deliver pixels. It is not the alpha cause: the replay with build v32
and blobs v17 reports no identity mismatch and the strip still returns 255, so the
right kernel produces it inside the worker. What differs from the tool is then the
device state other graphs leave behind in the worker process; the next experiment
loads the worker's other models after the strip's model and keeps them alive while
the strip spans run.

## Trial 7 (build v24, blobs v14, no hazard flushes): 2.3 updates/s

`native-live-v73` (registration v62, package v31 / 0.0.1331.0, `frame-blobs-v14`)
passed every chain gate; `native-repeated-input-v17-analysis` reports 2.3
verified updates/s (26 unique submissions, 25 verified on the display counter, 23
inside their scheduled interval). The driver flushed 229 batches instead of 931
(no hazard flushes; 125 presents, 71 writes, 28 per-draw routes, 3 limits) and
batched 4,794 draws against 29 unbatched. Each submission still waits about
470-530 ms. The trace puts the remaining time in two places:

| Component | Measured in trial 7 | Cause | Change |
| --- | --- | --- | --- |
| 28 per-draw routes, ~500 ms each | 14.3 s over the trial, about one per animation frame | d305ca5e/e455f7a5 sampling the 2880x1800 or 2880x96 texture point-sampled: the planner reported the whole texture because the interpolation drift guard added a noise symbol to every varying and the derivative evaluator refused any symbol it did not know | drift-guard symbols are marked pixel-independent (their screen derivative is zero): both draws now plan bounded windows (184x50 at 64 px tiles, 530x96 at 64 px for the 350x12 draw) and fit |
| Present flush: p50 109 ms, p90 969 ms | 93 s over 125 presents; kernel p50 75 ms of it, uploads p50 532 KB and p90 31 MB | about ten kernel requests per frame plus the frame's texture uploads | the kernel's per-pixel cost (below) and the upload volume |

The kernel ran 4,545 requests for 38.3 s in the trial. Its per-pixel cost is
unchanged (about 45 ns for the simple passes, 1.15 us for the acrylic strip), so
even with every draw batched the animation frame needs roughly 100 ms of NPU
time: 30 updates/s remains out of reach without a faster kernel (several pixels
per vector, which the 24,496 B code window allows only for the small shaders) or
more SHAVE engines per request.

## Trial 8 (build v28, blobs v14, derivative windows bounded): 2.8 updates/s

`native-live-v74` (registration v63, package v32 / 0.0.1332.0, `frame-blobs-v14`)
passed every chain gate; `native-repeated-input-v18-analysis` reports 2.8
verified updates/s (30 unique submissions, 30 verified on the display counter, 28
inside their scheduled interval). 4,810 draws were batched and 12 took the
per-draw route (ten 318-348 vertex glyph runs over the span budget and two draws
of an uncaptured pair, 1b8e912b/2ad136ed); the trial flushed 247 batches (146
presents, 85 writes, 11 per-draw routes, 4 limits, 1 read) with 1.25 GB of
uploads and 479 MB of writebacks, and the kernel ran 4,694 requests for 42.0 s.

The first five animation submissions were accepted 20-50 ms apart (latency
59-86 ms); from the sixth on each one waits 470-515 ms again. The present flush
median is 63 ms (p90 1,016 ms), kernel time per flush median 35 ms (p90 416 ms),
uploads p90 28 MB: the frame's cost is now the kernel work on about 1.6 Mpx of
simple passes plus the acrylic strip, the texture uploads of the frame, and the
present-flush round trip, with no per-draw route in the animation path.

Trials in this report: 0.7 (per-draw baseline), 2.1, 2.2, 1.9, 1.9, 1.9, 1.9, 2.3,
2.8 verified updates/s. Reaching 30 needs the kernel's per-pixel cost down by
roughly an order of magnitude (several pixels per vector for the small shaders,
all SHAVE engines per request, a larger carrier code window for the big ones),
fewer and smaller uploads per present, and a lower per-request floor.

## Trial 9 (build v32, blobs v17 with kernel identities): 2.4 updates/s, two draws unbatched

`native-live-v75` (registration v64, package v33 / 0.0.1333.0, `frame-blobs-v17`)
passed every chain gate; `native-repeated-input-v19-analysis` reports 2.4 verified
updates/s (27 submissions, 26 verified, 24 inside their interval; trials 7-9 span
2.3-2.8 with the same kernel path). 4,594 draws were batched and 2 took the
per-draw route, both 312-318 vertex glyph runs over the span budget; the 512x512
pass 1b8e912b now runs on the kernel. The worker checked the kernel identity on
every one of its 4,588 requests and found no mismatch. Flushes: 216 (122 presents,
87 writes, 4 limits, 2 per-draw routes, 1 read), kernel 40.7 s.

For the functional goal this is the state: every DWM shader pair captured in nine
trials has a verified route (25 native, one on the per-draw route because its blob
does not fit the code window, one glyph shape over the span budget), a wrong kernel
cannot deliver pixels, and a failed kernel run degrades to the per-draw route.

The acrylic alpha remains the one known pixel deviation. Further experiments on the
qualification side all returned the correct 250: the worker's other nineteen models
loaded after the strip's model and kept alive, and a model compiled (not imported)
and run on the NPU first. Only the C++ worker produces 255, even with a single blob
loaded. Build v33 adds a provider self-test (`NR_RENDER_FRAME_SELF_TEST=1`, `strict`
retires a disagreeing blob) that runs each blob's own fixtures through the worker's
provider at startup, which is both the next discriminator and a startup health check
for the deployed worker. It reproduces the problem on fixed inputs: with the same
blobs and fixture files that pass completely in the Python tool, the worker's
provider disagrees with the host on 5 of 9 fixtures of blob 7 (c71638aa), 5 of 9
of blob 8 (39799859) and 7 of 9 of blob 9 (ec1e65f6), the three long-running
shaders, and agrees on every small-shader fixture (the one "invalid-version"
difference is a header-only fixture compared past its header). The mismatches are
identical under whole-row copies, per-request host tensors and a cleared output
tensor. Build v34 runs every self-test group twice, dumps the first differing bytes,
and can delay the readback (`NR_RENDER_FRAME_SETTLE_MS`): the device answers the same
wrong bytes both times and the delay changes nothing, so it is not a readback race.
The differences are arithmetic (blob 8: 0x53 against 0x4a, 0xa1 against 0x9c in
one pixel) and in one blob 7 fixture a run of unwritten pixels (zeros where the host
has colour). Same bytes, same blob, same device, deterministic per process: what
differs between the worker and the Python tool is where the plugin places the input
rows and therefore the memory latency the SHAVE code sees, which is the signature of
a scheduling hazard the assembler did not flag (the toolchain is known to schedule
`CMU.CMB` too early after `VAU.MUL.f32`; `--fix-hazards` inserts the NOPs it does
report). The strip's alpha fits that too: a stale compare result would skip the
IEEE division fix-up and reproduce the pre-fix 255 exactly. Blob 7 does not fit the window at
any other optimisation level (-Os 24,560 B, -O1 25,104 B, -O2 30,320 B against
24,496), so that route is closed. The sizes themselves carry a hint: the three
failing blobs end within 32-192 bytes of the window (24,304, 24,496, 24,464 B) and
every passing blob ends at least 640 bytes short of it. Growing a passing blob (blob 6)
toward the window end with never-executed code refutes that reading: at 24,208,
24,368 and 24,464 bytes (the last one the size of a failing blob) it still passes
the worker self-test, so the failure belongs to the long shaders' code, not to its
position. On the tool side nothing reproduces the worker: the other nineteen models
loaded before or after ours and kept alive, and a model compiled on the NPU first,
all still give the host's result. What remains is the one call-sequence difference
between the two: the tool re-sets both carrier inputs on every request, the
provider zeroes the carrier's second input (the scatter "updates" plane) once and
never sets it again; build v36 re-sets it per request
(`NR_RENDER_FRAME_SET_UPDATES=1`), alone and together with the per-request input
tensor, and the self-test mismatches are unchanged. Every call-sequence difference
between the two programs is now equalised and the worker still differs, so the
remaining suspects are what the C++ runtime binding does with the request and the
device's memory for it (the blob 7 difference starts with a run of zero pixels in a
region the header counts as covered, which reads like writes that never reached the
host). Build v37 dumps the device output, the host expectation and the span of every
mismatching fixture (`NR_RENDER_FRAME_SELF_TEST_DUMP`). The dumps show the device's
answer in the worker is what an opaque alpha would produce where the host computed
a near-zero alpha (the acrylic quad: every covered pixel, the destination-out blend
leaving zeros), and in the other two shaders mostly the alpha lane again. Running
real fixtures of other blobs first in the tool, so the device carries another
kernel's leftovers, still gives the host's result there. The cause stays open; the
worker-side bisection of the shader program is the next experiment.

Until it is found, correctness wins: from build v38 the self-test is strict by
default whenever a blob's fixtures are deployed, so a blob whose fixtures disagree
with the host kernel serves no draw and its programs take the per-draw route. On
Delta that retires blobs 7, 8 and 9 (the acrylic strip, 39799859 and ec1e65f6),
which costs frame time but delivers the GPU path's pixels.

## Where the per-pixel time goes (device experiment, blobs 0 and 1)

Blobs 0 (five solid one-instruction shaders) and 1 (five textured one- and
two-instruction shaders) were rebuilt without the blend stage (`NR_EXP_NO_BLEND`)
and without the pixel stage (`NR_EXP_NO_PS`) and timed on the NPU over the same
fixture groups (twelve 128x128 tiles per request, best of four repeats):

| Blob, fixture group | Full | No blend | No pixel stage |
| --- | --- | --- | --- |
| 0 (solid), real-vertex group | 8.5 ms | 4.2 ms | 8.1 ms |
| 0 (solid), quad group | 12.0 ms | 7.6 ms | 7.9 ms |
| 1 (textured), real-vertex group | 28.2 ms | 20.1 ms | 8.0 ms |
| 1 (textured), quad group | 29.8 ms | 26.5 ms | 7.9 ms |

For the solid passes the blend is 35-50% of the time and the rasteriser and
interpolation the rest; the one-instruction pixel stage is negligible. For the
textured passes sampling is about 70% and the blend 10-30%. The frame's pixels are
mostly textured passes (595296ba, d056c6dc, 0a7b1c69), so the lever is the sampler
(a point-sampled, texel-aligned fast path and fewer loads per tap), then a
four-pixel blend; a vectorised arithmetic stage alone would change little.

## Correction: the qualification tool never compared pixels (2026-10-21)

The strict self-test of build v38 retired six healthy blobs on Delta before the
first replay batch: every blob's `invalid-version` fixture "mismatched". That fixture
is a span the kernel rejects (status 2), so the device leaves the pixel area
unwritten while the host expectation is zero-filled; the self-test compared the
pixel area anyway. Fixing that led to the comparison in
`tools/delta/qualify_frame_kernel.py`, which read the tile width and height from
bytes 16 and 20 of the *expected output* — the escape and loop-overflow counters,
both zero in a healthy fixture — so its live length was 64 bytes: the Python tool
had only ever compared the 64-byte output header. Every "qualify passed" recorded
for the frame blobs so far, and every experiment that concluded "the tool does not
reproduce the worker's difference", was a header-only check. The dumps saved by
build v37 confirm it: run through the tool's old comparison they all pass, while
13,507–54,065 of the 65,536 pixel bytes differ.

With the tile size taken from the input span and a header-only comparison for
rejected spans, the tool now reproduces the worker exactly on the deployed v17
blobs (`--patched-blob`, no rebuild):

| Blob | Fixtures | Mismatches (first offset) |
| --- | --- | --- |
| 0–6 | 29, 33, 27, 21, 15, 9, 15 | 0 |
| 7 (c71638aa) | 9 | quad rgba/bgra, split-windows, transparent-textures (1624/1627) |
| 8 (39799859) | 9 | quad rgba/bgra, split-windows (1627), real-vs rgba (38459) |
| 9 (ec1e65f6) | 9 | real-vs rgba/bgra (64/68), quad, split-windows (1624/1640), transparent-textures (55085) |

So the three 111–181-instruction shaders are wrong on the device in every harness,
not only in the worker; the worker-versus-tool investigation (call sequences, tensor
delivery, settle time, model ordering) was chasing an artifact. The strict default
of v38 retires exactly the wrong blobs, which was the right outcome for the wrong
reason, and v39 keeps rejected spans out of the comparison so healthy blobs stay.

What the failures look like, from the v37 dumps: the acrylic quad (blob 7,
destination-out blend) comes back all zero over the whole quad, i.e. the device
computed source alpha 1 where the host computed a small alpha; the blob 8 quad
differs in the alpha lane only, by 8–20 of 255, device larger; the blob 9 real-vertex
fixture differs in a 65x64 region in every lane.

Two hypotheses tested with the corrected tool:

- **Code window tail.** The failing blobs end 32–192 bytes short of the 24,496-byte
  window and the passing ones 640 bytes or more. The ELF layout (no read-only data;
  helpers after the generated row functions) puts `nr_tap`, `nr_mirror01` and
  `nr_blend_pixel` in the last 1.5 KB of blobs 7–9. But blob 6 padded with a dead
  function to 24,464 bytes moves its live `nr_tap` and `nr_v4_floor` to
  23,440–24,438 and still passes a full-pixel comparison, so code at the window tail
  executes correctly. Refuted.
- **A broken out-of-line helper.** The helpers only the failing blobs use are
  `nr_v4_movc`, `nr_v4_frc` (and through it `nr_v4_floor`), `nr_v4_min/max`,
  `nr_v4_sat`, `nr_v4_cmp_lt/eq`, `nr_v4_dp`. A probe kernel evaluated each helper
  on 12,000 random records (normal, special, random-bit-pattern and integer-valued
  lanes) and the host evaluated the same records. Every lane whose inputs and
  outputs are normal numbers agrees. The differences are all edge cases of the
  device's float unit: it flushes subnormal operands and results to zero
  (`div`: a subnormal divisor becomes 0, so x/subnormal is inf or NaN on the device
  and finite on the host; `max`: a subnormal operand comes back as 0; `min` and
  `cmp_lt`: a subnormal compares equal to zero), and it canonicalises NaN payloads
  (`frc`, `dp`). `cmp_ge` and `cmp_eq` flush explicitly on both sides and agree.
  None of this involves the values a DWM shader sees (texel fractions of 1/255 and
  their products are far from the subnormal range), so the helpers are not the
  cause either; the subnormal semantics are recorded as a host/device difference to
  resolve (flush on the host as SM4 does).

The remaining suspect is the generated row function itself (4–6.5 KB for these
shaders against 1.9–2.5 KB for the passing ones): register pressure, spills or
scheduling inside one large function. A tapped build of blob 9 (eight instructions
per build, each pixel storing one lane of one instruction's result into a spare
output area, no early exit) is scanning all 181 instructions against the host; the
first instruction whose result differs names the operation.

The replay of build v38 also exposed a worker defect: a batch command recorded
without its per-draw route (`legacy_abi` 0, as in every batch of the trial-2
capture, which predates the field) made the fallback build a per-draw request with
abi 0, which the provider rejected and the batch failed. The route selection now
lives in `legacy_route.hpp`, shared by the driver and the worker: the driver records
it and the worker derives the same route for a command that has none. The shared
version also classes mirror- and border-addressed samplers as general resources, as
the per-draw path always did; the recorded route had kept the clamp-only selection
from before the batcher accepted mirror addressing, so a mirror-sampled draw that
fell back would have been sampled with clamp addressing.

## Root cause of the long-shader failures: the SHAVE swizzle lowering (2026-10-21)

The tapped scan of blob 9 (ec1e65f6, 181 instructions) against the host found the
first divergent instruction at 154, an add whose operand is the negated z,w lanes of
register r3 swizzled as (2, 3, 2, 2): the device read 0 (in another build NaN) where
the host had the values r3 held since instruction 5, although every earlier read of
the same lanes agreed. Tapping r3 itself right before the instruction made the
divergence disappear — a layout-dependent fault. Padding passes that delay every
consumer of a VAU or CMU result, every write to a just-written register, or every
reload of a just-stored stack slot (new `--pad-raw-*` and `--pad-stack-distance`
options of `tools/build_ptl_kernel.py`) changed the wrong value but never made it
right, so this was not a latency hazard.

The generated shader code reaches the vector unit through two macros the runtime
barely uses: `NR_V4_SET` (a lane insert) and `NR_V4_SWZ` (a swizzle, spelled with
`__builtin_shufflevector`). Rewriting each in a semantics-preserving way on both
sides (the host output is byte-identical under every rewrite) and tapping the
acrylic shader's final value on the device:

| Variant of blob 7 (c71638aa, tap at the last instruction) | Device vs host |
| --- | --- |
| as generated | lanes x, y, z agree; alpha wrong at every pixel (device 1.0) |
| lane insert as a masked select | alpha still wrong |
| swizzle as explicit lane extraction (`nr_v4_make(a[x], a[y], a[z], a[w])`) | **all lanes agree at every pixel** |
| both rewrites | every lane wrong (the select lowering fails too) |

moviCompile lowers `__builtin_shufflevector` with repeated indices such as
(0, 1, 0, 0) and (2, 3, 2, 2) through `CMU.CMB` combines whose result is wrong in
these long functions; the same compiler's lowering of four lane extractions into a
new vector is correct. With the swizzle macro changed in `frame_core.h` (SHAVE path
only; the host path already used scalar swizzles), the production sources of blobs
7, 8 and 9 pass all 27 fixtures on the device with the full pixel comparison, at
24,304, 24,496 and 24,416 bytes (blob 9 shrank by 48 bytes), and the assembler no
longer requests the `CMU.CMB` hazard NOPs that `--fix-hazards` existed for. The
tapped blob 9 agrees with the host at its final instruction on every covered pixel.

Consequences: bundle v18 rebuilds all blobs from the corrected core and qualifies
them with the corrected tool; the worker's strict self-test then keeps all of them,
so the acrylic strip and the two other long shaders return to the kernel route with
GPU-identical pixels. The small shaders were never wrong: their generated code holds
few repeated-index swizzles, or none that the lowering mishandled.

## Bundle v18 and worker v40 (2026-10-21)

Bundle v18 regenerates all eleven blobs from the corrected core and qualifies them
on Delta with the corrected tool (full pixel comparison, one request per group of
twelve fixtures):

| Blob | Programs | Text bytes (v17 → v18) | Fixtures | Mismatches |
| --- | --- | --- | --- | --- |
| 0 | 6 solid/simple | 20,416 → 20,496 | 29 | 0 |
| 1 | 5 textured | 23,504 → 23,600 | 33 | 0 |
| 2 | 4 (incl. e87ed647) | 22,240 → 22,320 | 27 | 0 |
| 3 | 3 | 22,816 → 22,848 | 21 | 0 |
| 4 | d305ca5e, 1b8e912b | 21,536 → 21,568 | 15 | 0 |
| 5 | e455f7a5 | 19,808 → 19,792 | 9 | 0 |
| 6 | b7f4e6d0, 2a28d3fe | 23,856 → 23,808 | 15 | 0 |
| 7 | c71638aa | 24,304 → 24,304 | 9 | 0 (was 4) |
| 8 | 39799859 | 24,496 → 24,496 | 9 | 0 (was 4) |
| 9 | ec1e65f6 | 24,464 → 24,416 | 9 | 0 (was 6) |
| 10 | e3ebf67c | 28,912 | — | does not fit the window; per-draw route |

The small shaders needed no hazard NOPs any more except blobs 0–4 (one each, as
before). Deployed as `frame-blobs-v18\deploy` with identities unchanged (they hash
the per-blob program header, not the core).

Worker tree v40 carries the rejected-span self-test rule, the shared per-draw route
selection and the swizzle fix in its host copy of the core (the host path was never
affected). Build v39 failed to compile because the route header was first included
inside the driver's anonymous namespace; v40 includes it at file scope.

## Replay of the trial-2 capture with worker v40 and bundle v18 (2026-10-21)

The 1,335-request capture from trial 2 replayed through worker v40 with bundle v18
(`frame-replay-v25-085958` on Delta):

| Measure | Value |
| --- | --- |
| Batches replayed (recorded / converted per-draw) | 1,002 / 333 |
| Converted batches within tolerance of the host reference | 333 of 333 (305 bit-exact) |
| Beyond tolerance / incomparable | 0 / 0 |
| Commands / kernel / fallback | 5,165 / 4,492 kernel, 185 transparent no-ops |
| Fallback routes | 122 `kernel blob 10 unavailable` (e3ebf67c), 2 `span budget` |
| Blobs retired by the startup self-test | 0 (all ten blobs, 0 mismatches) |
| Kernel time | 38,973 ms over 4,747 requests |

The acrylic shader c71638aa now runs on the kernel for all 108 of its draws (tiles
32/64/128); before the swizzle fix the self-test retired blobs 7–9. The only
per-draw fallbacks are the blur shader whose blob does not fit the window and two
span-budget overflows.

## Trial 10 (build v40, bundle v18): the fallback packet for mirror addressing

Generation-11 chain: package v35 (0.0.1335.0), registration v66 (qualification
passed, 16 frame batches, 8 receipts), readiness observer v158. The GPU 3D
utilisation sampler (`\GPU Engine(*engtype_3D)\Utilization Percentage`, 2 s
interval) ran alongside the live trial for the first time.

The trial ended 5 s into the admission: `NPU worker stopped before native frame
evidence`. The child provider's log shows batch 1 executing on the kernel (5
commands, 33 kernel requests, 364 ms) and batch 2 failing at its first per-draw
fallback:

```
{"frame_batch_fallback_begin":true,"route":"fallback:kernel blob 10 unavailable","abi":11,"reserved":0,"flags":1388295}
invalid indexed NPU exchange packet
```

The captured request (`failed-request.bin`, 135 commands) holds two blur draws
(e3ebf67c, 192×64 targets, blend `plus`, 6 vertices) whose sampler uses mirror
addressing (`address_u = address_v = mirror`). Their recorded route is abi 11 with
flags 0x152f07. The per-draw encoder requires clamp addressing unless the packet
carries abi 12 (clamp/mirror bits) or 14 (two-bit modes with border colours); abi
11 with a mirror sampler cannot be encoded, so `require` threw before the request
reached the per-draw executor, the child reported the batch as failed, the parent
pipe completed with error 233, and the bootstrap stopped the trial. Recovery
restored the display and removed both packages (cleanup passed).

Root cause: the driver's own per-draw path upgrades the abi to 12 or 14 after its
route selection (`indexed_draw.hpp`: `if(addressed) abi=12; if(bordered) abi=14;`),
but the shared `legacy_route` that records the route into each batched draw stopped
at the blend selection. Every mirror- or border-addressed draw that falls back
therefore carried an unencodable route. The trial-2 capture replayed cleanly
because its blur draws used clamp addressing.

Fix (worker tree v41): `legacy_route` takes `bordered` next to `addressed` and
applies the same two upgrades before the component-coverage override; the driver
feeds both from the bound samplers, and the worker feeds them from the batch's
texture bindings. A replay of the failing capture (its second request appended to
the index by hand, since the worker never recorded a response for it) still failed
with v41: the capture carries the routes driver v40 recorded, and the worker ran
the recorded route. Tree v42 therefore derives every fallback route from the
batch content with the shared function and runs the derived route; a recorded
route that differs is logged as `frame_batch_route_disagreement` with both pairs,
so every trial now measures whether driver and worker agree. The GPU sampler
recorded 19 samples during the 42 s trial (idle desktop, 1.1 % average), which is
not an offload measurement.

Verification of tree v42 on Delta (`frame-replay-v30-092310`): the trial-10
capture, both requests, replays completely: 140 commands, 123 kernel requests,
the two blur draws on the per-draw route (56 and 32 provider requests), no
rejected packet. The worker logged the expected disagreement for each:

```
{"frame_batch_route_disagreement":true,"recorded_abi":11,"recorded_flags":1388295,"derived_abi":12,"derived_flags":1388295}
{"frame_batch_fallback_begin":true,"route":"fallback:kernel blob 10 unavailable","abi":12,"reserved":0,"flags":1388295}
```

The trial-2 capture replayed through v42 (`frame-replay-v31-092319`) exactly as
through v40: 333 of 333 converted batches within tolerance, 124 fallbacks, no
route disagreement. Its 122 blur fallbacks already carried abi 12 because they
come from per-draw packets the driver's per-draw path had encoded; the batch
recording lambda the shared route replaced on 2026-10-21 lacked the two upgrades
as well, so the defect was latent since the recorded route was introduced, and
trial 10 was the first live run in which a mirror-addressed draw fell back inside
a batch.

## Trial 11 (build v42, bundle v18, package v36): 2.5 updates/s, GPU 3D engine idle

Generation-12 chain (registration v67 qualified with 16 frame batches), live
trial `native-live-v78` / `ptl-native-live-v42`, 660 s admission, request
capture on (218 files, 1.3 GB, private pixels stay on Delta). Passed end to end
with cleanup; one provider generation; no worker stderr.

| Measure | Trial 9 (v32, blobs v17) | Trial 11 (v42, bundle v18) |
| --- | --- | --- |
| Verified unique updates/s (300 requested at 30 Hz) | 2.4 | 2.5 (26 of 300) |
| Mouse sent-to-display median / p95 | — | 94 ms / 1,497 ms |
| Frame batches / commands | — | 424 / 6,079 |
| Commands on the kernel / copy / clear / no-op / fallback | — | 4,772 / 595 / 405 / 195 / 112 |
| Fallbacks | blobs 7–9 retired | 112, all `kernel blob 10 unavailable` (e3ebf67c), abi 12 |
| Startup self-test | retired 7–9 | 10 blobs, 0 mismatches, 0 retired |
| Route disagreements (driver vs worker) | not measured | 0 of 112 |
| Kernel / infer / copy / host time | — | 42.7 s / 41.9 s / 0.7 s / 81.9 s |
| Flushes (UMD) | — | 204: 117 present, 76 write, 5 legacy draw, 4 limit |
| Flush wall p50 / p90 / max | — | 265 / 1,000 / 3,500 ms |
| Draws batched / not batched | 2 unbatched | 5,096 / 5 (span budget, d8e0b497 348 vertices) |
| Acrylic c71638aa on the kernel | per-draw | 119 draws |
| Physically submitted frames | — | 170 |

The acrylic, blur-adjacent long shaders now run on the kernel, the only
per-draw fallback left is the blur blob that does not fit the window, and driver
and worker agreed on every recorded route. The update rate did not move: the
per-flush wall (p50 265 ms, p90 1 s) is still the limit, and present-triggered
flushes carry 66 of the 92 s of flush time.

### GPU 3D engine utilisation during the NPU-rendered desktop (first measurement)

`\GPU Engine(*engtype_3D)\Utilization Percentage` summed over all instances,
sampled every 2 s for 720 s from before admission until after restoration
(`vm\gpu-samples-live78.jsonl`, 341 samples):

| Statistic | Value |
| --- | --- |
| Average | 0.13 % |
| p50 / p90 / p99 | 0 / 0.21 / 1.18 % |
| Samples above 10 % | 1 (18.9 %, dwm.exe, 16:39:21Z) |
| Non-zero samples | 114 of 341 |

The admission ran 16:27–16:39Z; the two highest samples (5.9 % and 18.9 %) fall
at 16:39:19–21Z, when the topology was restored and DWM resumed rendering on the
iGPU. During the NPU-rendered interval the 3D engine stayed below 1.2 % in every
sample. Caveats: this is the 3D engine only (copy and video engines were not
sampled), the desktop ran the controlled trial stimulus rather than ordinary
applications, and the physical bridge's presentation work is not separated out;
it is evidence that composition left the 3D engine, not a Tahoma benefit
measurement.

## Long-run attempt 1 (generation 13, 2220 s requested): ended by the topology helper at 600 s

The chain generator gained `--monitor-seconds` (scaling the presenter, stimulus,
relay, cursor watchdog, scheduled-task limits and the topology/native process
waits that were fixed against the 660 s base) and `--registration-generation`
(reusing registration v67). The 2220 s run (`native-live-v79`, admission v89)
ran the NPU desktop for 10 minutes and then failed `Frame relay exited during
presentation`: the relay (`--seconds 2150`) had completed normally at 600 s
because the topology helper, launched with its own `-Seconds 600` that the
generator did not scale, restored the physical topology at that point. The
admission was stopped by the live controller and restoration passed. GPU 3D
engine over the 10 minutes: 308 samples, average 0.03 %, maximum 0.61 %.

The helper's duration is now scaled with the run as well. Both the helper
(`ValidateRange(1,2160)`) and the relay (`assert seconds<=2160`) cap a run at
36 minutes; longer runs need new versions of both and a check of the topology
binary they drive, which is the step after the lease renewal below.

## Trial 12 (generation 14, build v43, package v37): 36 minutes with lease renewal

Worker tree v43 adds in-place lease renewal: the consumer's `Admission` re-reads
its mapping when the cached deadline is within five minutes and accepts only a
renewal (issued and deadline ticks advanced, everything else identical), the
worker follows the compositor's lease every 20 s, and the generated bootstrap
rewrites both leases' ticks and re-arms the recovery task's one-shot trigger
when less than ten minutes of a grant remain (`native_lease_renewal_v1.py`).
Registration v69 qualified the v43 worker; the admission (`native-desktop-admission-v90`,
`native-live-v80`) ran 2160 s, the longest NPU-rendered desktop so far.

| Measure | Trial 11 (660 s) | Trial 12 (2160 s) |
| --- | --- | --- |
| Passed / cleanup / recovery task removed | yes / yes / yes | yes / yes / yes |
| Verified unique updates/s | 2.5 | 2.4 (26 of 300) |
| Frame batches / commands / fallbacks | 424 / 6,079 / 112 | 744 / 9,363 / 202 (all blob 10, abi 12) |
| Kernel / host time | 42.7 s / 81.9 s | 65.5 s / 133.8 s |
| Draws batched / not batched | 5,096 / 5 | 5,319 / 0 |
| Route disagreements / self-test mismatches / stderr | 0 / 0 / 0 | 0 / 0 / 0 |
| Observed frames (distinct pixel hashes) | — | 334 (190) |
| Physically submitted frames | 170 | 342 |
| Flush wall p50 / p90 / max | 265 / 1,000 / 3,500 ms | 281 / 1,000 / 4,968 ms |
| Lease renewals (controller) | — | 913 (storm, see below) |
| Worker lifetime extensions | — | 2 (remaining 599 s and 499 s) |
| GPU 3D engine p50 / p90 / p99 (1,057 samples over 37 min) | 0 / 0.21 / 1.18 % | 0 / 0.11 / 0.37 % |
| GPU 3D maximum | 18.9 % at restoration | 22.0 % at restoration (dwm.exe, 17:34:05Z) |

The first grant (2220 s) outlasted the admission, so this run shows the
controller renewing and the worker following, not the consumer re-reading a
renewed lease; generation 15 (first grant 1200 s, same 2160 s run) exercises
that path and the worker's expiry path.

Defect found and fixed: from 27 minutes on, the controller renewed on every
monitor iteration (913 renewals in nine minutes, each spawning a PowerShell to
re-arm the recovery task) because a grant capped by the run's own end leaves the
deadline inside the renewal margin. The planner is now a pure function that
renews only when the extension is worth at least a minute and thirty seconds
have passed since the last renewal (`tools/delta/test_native_lease_renewal.py`).
The run was not affected beyond a slower monitor loop.

Runs beyond 36 minutes remain capped by the topology helper, the frame relay
and the topology control binary (2160 s each); they can be lifted now that
the lease no longer bounds an admission.

## Trial 13 (generation 15, first grant 1200 s): the indirect display driver's own lease check

Same build, package and registration as trial 12; the first grants and the
worker lifetime were shortened to 1200 s so every renewal path would run inside
the 2160 s admission. The controller renewed once at 600 s remaining (1620 s
granted, no storm), the worker followed (`worker_deadline_extended`, 1619 s
remaining), DWM's driver kept flushing batches on the NPU to the end (282
flushes, the last at sequence 450, execution device NPU), and the admission,
bootstrap and recovery all passed over the full 2160 s. The live controller
failed the run at its final check, `Continued delivery and fresh physical
observation not qualified`, because the indirect host stopped publishing frames
at 17:55:42Z, 1190 s after the first grant was issued: 181 frames, none after
1200 s.

The indirect display driver (package v2, signed on 2026-10-15) carries its own
copy of the lease header from before renewal existed. Its `native_ready()` calls
`Admission::verify()` on the record it read at admission, so at the first
grant's deadline every frame-loop step returns access denied and the display
goes stale, while DWM's driver and the worker, which hold the renewing header,
continue. The fix is a rebuilt indirect display package (v3) from the same
sources with the tracked lease header; the chain generator gains the package
and version as options. Measurements for the record:

| Measure | Trial 13 |
| --- | --- |
| Admission / bootstrap / recovery | passed / passed / passed (2160 s) |
| Renewals / worker extensions | 1 / 1 |
| Frames published (distinct) | 181 (142), last at 1190 s |
| DWM flushes / batched draws / unbatched | 282 / 2,634 / 0 |
| Verified updates/s (first 10 min stimulus) | 2.3 |
| GPU 3D engine over 2215 s | average 0.04 %, maximum 22.5 % at restoration |

Open fidelity item found by the new route contract test
(`legacy_route_test.cpp`, 63,840 draw states through the shared route, the
header check and the encoder): the batch recorder stores a binding's address
modes but never its border colour, so a border-addressed draw that falls back
samples a zero border where the driver's own per-draw path would carry the
sampler's colour. No trial has shown a border-addressed fallback yet; it is
noted here so the next fallback evidence is read with it in mind.

## Harness changes after trial 13 (2026-10-21)

The 36-minute caps are lifted for the generation-17 run. Built and qualified
on Delta from the same sources with only the duration limits raised: topology
control v14 (four hours), presenter v22 with the frame reader qualified on it
(`live-frame-reader-v13`) and its input-contract qualification, output observer
v6 with its self-test qualification; the generator emits topology helper v14,
frame relay v9 and cursor watchdog v3. The indirect display package v3
(0.0.1503.0) is built from the v2 sources with the renewing lease header; all
four of its contract tests pass and it is signed with the lab certificate.

The orchestration that cost most of the day is replaced: Delta runs each
sequence from one JSON plan as a detached scheduled task and rewrites one
status document per step (`tools/delta/run_delta_sequence.ps1`,
`delta_sequence.py`); the engine sampler records the GPU 3D engine and the CPU
shares of dwm, the NPU worker and the indirect host beside every trial; the
replay-corpus gate (`replay_corpus.ps1`) replays the latest admissions'
captures on a build before any live run; and the route contract test runs on
every lab build. Two qualification attempts failed only because the OLED screen
saver had taken the console, so every plan now stops it first.

Three more finite-duration pins surfaced while starting the two-hour run, each
ending an attempt within a minute of admission and each now fixed in the
generator: generation 17's wrapper refused to start while the readiness
observer's inspect-only probe process existed (the wrapper no longer refuses on
that name; the admission script keeps its exact-argument check); generation 18's
cursor watchdog verified the topology control binary against the v13 build proof
while the helper ran v14 (the live script now follows the helper, with the
cursor-guard procedure re-run on v14 as qualification v6, passed); generation
19's presenter rejected the 7,125 s input stimulus from a second cap inside its
input-stimulus mode (presenter v23 raises every duration cap to four hours).
Every attempt restored the display cleanly; none produced measurements.

## Trial 14 (generation 20, 7200 s requested): DWM crashed at 2342 s

Presenter v23, output observer v6, frame reader v14, topology control v14 with
the cursor guard re-qualified on it (v6), indirect display package v3 and a
2400 s first grant. The admission (`native-desktop-admission-v96`, live v86)
ran on the NPU for 39 minutes: 272 frames observed, 288 batch flushes, the
controller renewed both leases at 30 minutes (2340 s granted) and the worker
extended its lifetime. At 19:16:44Z, 2342 s after the admission started and
58 s before the first grant's original deadline, `dwm.exe` crashed inside
`dwmcore.dll` (exception 0x889800b0, fault offset 0x1d3cf8). The indirect
display's consumer then exited with DXGI access lost, the relay ended, the
indirect host died and the bootstrap stopped at `host.verify()`. Recovery ran
from the bootstrap's own `finally` and passed; the topology helper reported an
inexact restore because DWM had already restarted, and the screen saver crashed
as collateral. The recovery task did not fire: the Task Scheduler log holds no
entry for it, and the device removal in the logs is the bootstrap's own.

Our driver's trace in DWM shows nothing abnormal before the crash: the last
flush (sequence 2244) executed on the NPU in 63 ms and its present callback
returned success, after which DWM rotated resource identities as usual. This
is the first DWM crash in the trials, three minutes past the longest previous
admission (2160 s), so it is either time- or count-dependent on our side or a
DWM reaction to something our driver returned that the trace does not record.
No dump was kept (WER stored only `Report.wer`), so Delta now keeps local mini
dumps for `dwm.exe` (`vm\dumps`, five at most) for the next run. The IDD v3
deadline itself was not reached, so its refresh remains unproven.

The replay-corpus gate on worker v43 (`replay-corpus-v43`, the three latest
admissions' captures) passed: trial 14's 300 recorded requests replay with no
rejected packet, no retired blob, no route disagreement and every converted
batch within tolerance (56 s), as do the two short generation-18 and -19
captures. The crash is therefore not in the batch path the worker executes.

Cause of the crash's timing: the OLED Care screen saver started at
19:16:44.120Z, the same second DWM faulted, and Winlogon logged the
screen-saver-start notification (event 6) at 19:16:44Z and the stop (event 7)
at 19:16:53Z when the saver itself died with DWM. The saver's idle timer fired
about 27 minutes after the trial's input stimulus ended; trials 12 and 13 were
too short to reach it. Screen saver activation switches DWM to another desktop
and creates new composition targets, and with our adapter as the primary
renderer that transition took DWM down. This is the screen-saver robustness
case the plan lists under gate 4, now reproduced with a time to crash; the next
run keeps a DWM mini dump so the failing call is identified.

## Trial 15 (generation 21, 1200 s first grant): renewal proven at every layer

Same components as trial 14 with the first grant shortened to 1200 s and DWM
fresh after its restart. The controller renewed at 19:35:55Z (600 s remaining,
2340 s granted), the worker extended its lifetime, and the indirect display
package v3 kept publishing frames past its original deadline of 19:45:54Z:
frames observed at 19:46:00, 19:47:00 and 19:48:00Z with the indirect host's
readbacks continuing. With trial 13's DWM-side result this closes the lease
renewal chain: controller, worker, DWM's driver and the indirect display driver.
The trial's input stimulus failed at start (`relative mouse origin failed`, an
environmental pointer failure the generation-20 run did not show), so the
desktop idled from the beginning; the run continues toward the screen saver's
idle trigger with DWM mini dumps enabled.

Trial 15 ended the same way at 20:05:04Z, 2350 s after admission: the screen
saver started (Winlogon event 6), the indirect host died within five seconds
(`host.verify()`), the relay ended, and Windows Error Reporting then logged 49
`LiveKernelEvent` 141 reports, the video engine timeout. DWM itself did not
crash this time. Two runs, one trigger: the OLED Care screen saver activating
about 39 minutes into the admission whether or not the trial's input ran. With
our adapter as the primary renderer the saver's full-screen rendering ends in a
display engine timeout; which engine timed out is the question the dumps must
answer. No user-mode dump was written in trial 15 because the dump folder sat
under the user profile, which DWM's own account cannot write; dumps now go to
`C:\ProgramData\T8-NpuRender-Dumps` for both `dwm.exe` and `WUDFHost.exe`, and
a helper starts the saver at a chosen second so the case reproduces in minutes.

## Trial 16 (generation 22): the screen saver crash reproduced in six minutes, with a dump

A 660 s admission with the saver requested through the system path five minutes
in (`trigger_screen_saver_later.ps1` sets the user's saver and broadcasts
`WM_SYSCOMMAND SC_SCREENSAVE`; launching the `.scr` directly only opens a window
on the default desktop and changes nothing). Winlogon logged the screen-saver
start at 20:15:11Z and `dwm.exe` crashed the same second with the trial-14
signature, this time leaving a full dump (`dwm.exe.8220.dmp`, 685 MB). The
debugger's verdict:

```
ERROR_CODE: (NTSTATUS) 0x889800b0 - DWM failed to create a graphics device.
KERNELBASE!RaiseFailFastException
dwmcore!MilCompositionEngine_CreateChannel+0x18e349
dwmcore!MilCompositionEngine_Initialize+0x18068
```

The screen-saver desktop switch makes DWM initialise another composition
engine and create another graphics device on the primary adapter, ours, and
that creation fails, so DWM fails fast. Our trace in that DWM process records
four `open_adapter` entries and only the first admission's `create_device`, so
the second device never reached our device-creation entry: the failure is in
the adapter open or in the runtime and kernel steps before it. This is the
first product defect of gate 4 with a six-minute reproduction and a dump.

Root cause, from the dump and the driver: `OpenAdapter10` verifies the
compositor's lease before anything else, and that verification required the
input desktop to be "Default" for the compositor scope. A screen-saver switch
(and equally the lock screen or UAC's secure desktop) moves the input desktop
away, so when DWM created a graphics device for the new desktop the adapter
open returned unsupported, before even tracing, and DWM failed fast. The rule
added nothing for the compositor, which the lease already binds by identity,
session and SID; it is now applied only to interactive clients. Build v44 and
package v38 (generation 23) carry the change into the same six-minute
reproduction.

## Trial 17 (generation 23, build v44, package v38): DWM survives the screen saver

Same six-minute reproduction on the fixed driver. At the saver request
(Winlogon event 6 at 20:27:05Z) DWM did not crash and no dump was written; its
trace shows a second `create_device` and `device_created` on our adapter, the
graphics device it could not create before, and frames continued (118 observed).
The admission still ended a few seconds later, this time by the harness: the
frame relay and presenter are trial tools bound to the Default console by
design, they exited at the desktop switch, and the live controller requested
restoration, which passed. The product path (DWM, worker, indirect display) is
therefore checked next with the admission alone across a saver request, and the
long soak keeps the presentation harness with the saver timer raised until the
presenter can re-attach after a desktop switch.

## Trial 18 (generation 24): the admission alone survives the screen saver

The desktop admission without the presentation harness (no topology helper,
relay or presenter; the indirect display stays secondary), 660 s, with the
saver requested through the system path at 20:32:44Z. The admission kept
running: the bootstrap stayed passed, no dump was written, and the frame count
rose from 9 to 276 within ninety seconds of the request because DWM composed
the saver's animation for the indirect display through our adapter at the NPU
path's usual rate. With trial 17 this separates the two remaining effects of a
desktop switch: DWM's device creation (fixed in build v44) and the indirect
display's swap chain, which is lost only when the indirect display is primary
with the presentation bridge attached (trial 17's access-lost exit). The
driver's reassignment after such a switch is the next product item; the
two-hour soak meanwhile raises the OLED saver timer for its duration.

Trial 18's ending, from the dump and the worker log: DWM crashed at 20:37:17Z,
four and a half minutes into the saver, in `CD3DDevice::CreateD3D11Device`
under `CLegacyRenderTarget::EnsureSwapChain`, the same "failed to create a
graphics device" fail-fast, but with a different cause. The worker had just
exited with `request recording budget exceeded`: the saver's animation drove
841 batches of about 20 MB each through the capture in four minutes and filled
its 16 GiB budget, the recorder threw, the worker stopped, and DWM's next device
creation found no exchange. Two items follow. The recorder must stop recording
and keep serving (worker change). More important for the product, the loss of
the worker must never make DWM's device creation fail: the driver should keep
creating devices while the lease holds and run on its CPU fallback until the
worker returns. During the saver the indirect display cycled through four swap
chain releases and reassignments and kept producing (824 frames, 530 distinct),
and the GPU 3D engine sat at 7.9 % (p50) because the saver itself rendered on
the iGPU for the physical panel. The two-hour soak restarts without capture.

Harness note: the input stimulus (`--input-stimulus`) failed at its start with
`relative mouse origin failed` in generations 21 and 26 and ran in generations
20 and 23, with the same presenter build each time; it depends on the pointer's
state when it starts. A run without it still exercises the admission, the
renewals and the presentation path, and its live verdict is read from the
admission and bootstrap reports; the stimulus-free wrapper avoids the gate.

## Trial 19 (generation 26, two-hour soak attempt without capture): 39 minutes, ended by the saver again

Build v44 and package v38, 1200 s first grant, no request capture, and the OLED
Care timer raised to 86,400 s in the registry for the run. Two renewals passed,
61 frames were observed on an idle desktop (the input stimulus failed at start
as in generation 21), and at 21:19:50Z, 2350 s in, Winlogon logged the
screen-saver start regardless of the raised timer: the ASUS service does not
re-read the value while running. DWM did not crash (the build v44 change
holds); the indirect display's consumer exited on access lost, the relay and
presenter ended with the desktop switch, and the controller restored the
display. The next soak stops the ASUS OLED Care processes for its duration
(`suspend_oled_care.ps1`) and restarts them after; the harness-side item, the
presenter and relay re-attaching after a desktop switch, stays open.

## Trial 20 (generation 27, build v45, package v39, through the switch): clean 660 s

The first admission started by `npu-render desktop enable`, on the package the
switch had prepared and registered itself. It ran its full 660 s and passed
with cleanup (154 frames observed, no crash, no dump). It was meant to be the
worker-loss probe, but the worker-stop helper had detached its timer from the
SSH session and never fired; the helper is now a scheduled task and the probe
reruns. Build v45 carries the recorder change and the device creation that
tolerates a lost worker.

## Trial 21 (generation 27 rerun, build v45): the worker's loss no longer takes DWM down

Through the switch again, 660 s, with both worker processes stopped by a
scheduled task at 21:44:02Z, four minutes into the admission. DWM stayed up
(no crash event, no dump, the same DWM process before and after), the
bootstrap detected the loss within seconds (`NPU worker stopped before native
frame evidence`) and restored the display cleanly. Before build v45 the loss of
the worker made DWM's next device creation fail and DWM fail fast (trial 18).

## Trial 22 (generation 28, admission v105, two-hour soak through the switch): 40 minutes, and the trigger found

Started 21:45:32Z by `npu-render desktop enable --monitor-seconds 7200
--initial-grant-seconds 1200` on build v45, package v39, frame blobs v18 and
IDD package v3, with request capture on and the ASUS OLED Care services stopped
by `suspend_oled_care.ps1` beforehand. The admission renewed its lease twice
(21:55:40Z and 22:24:39Z, 2340 s each), the worker followed, 278 frame batches
ran on the NPU, the relay published 290 frames and the presenter submitted and
displayed 290 (`displayed_present_count` 290 at its last observation). DWM did
not crash and left no dump. At 22:24:41.8Z Winlogon logged the screen-saver
start (event 6) and the run ended the same way as trials 19 and 21: the cursor
watchdog's `OpenInputDesktop` assertion failed at 22:24:41.57Z, the presenter
exited with `live interactive desktop changed`, the output observer with
`observer desktop changed`, the relay completed cleanly at 22:24:41.68Z, the
live wrapper threw `Frame relay exited during presentation`, and the bootstrap's
own recovery restored the display by 22:25:08Z (passed). The admission report
reads `completed=True passed=True stopped_by_controller=True`.

The trigger, read afterwards on the live machine: the process that started at
22:24:41.81Z is `C:\Windows\ASUS\ScreenSaver\OLED Care Screensaver.scr /s`,
launched by winlogon.exe through the ordinary screen-saver path, and
`AsusOptimization.exe` had restarted itself at 21:45:07Z, 25 s after the
suspend helper stopped it and before the admission began. Its setting
`OLEDProtectScreensaver` under `ASUS System Control Interface\AsusOptimization\
ASUS Keyboard Hotkeys` is 1800 s, and the stimulus phase's last injected input
was about 30 minutes before the event. The Windows side shows nothing: the
registry `SCRNSAVE.EXE` is empty, the live session's `SPI_GETSCREENSAVEACTIVE`
is 0 and its timeout 0 (read from an interactive one-shot task,
`screen_saver_session.ps1`), yet `SPI_GETSCREENSAVERRUNNING` was 1 because the
ASUS saver was still running eight minutes later. Stopping the ASUS services is
therefore not a control; the saver is the machine's normal behaviour after 30
idle minutes and the harness has to live with it, as DWM, the worker and the
indirect display already do.

### Harness changes after trial 22: the live tools wait out a desktop switch

The presenter (`native-present-v24`), the topology control binary
(`native-topology-control-v15`), the physical output observer
(`native-output-observer-v7`) and the cursor watchdog
(`cursor_recovery_watchdog_v4.py`) no longer end when the input desktop
leaves `Default`. The shared `session_gate.hpp` gained `wait_console_default`,
which logs one `desktop_departed` row, polls every 250 ms until the signed-in,
unlocked Default console desktop is back, logs `desktop_returned` with the
absence in milliseconds, and keeps the tool's own stop conditions live while
waiting (presenter window close or deadline, observer stop file, topology
escape hotkey and controller stop file, watchdog owner exit and stop file).
The presenter also treats `DXGI_STATUS_OCCLUDED` from `Present` as a skipped
frame instead of a fault, and the observer re-creates its desktop duplication
after `DXGI_ERROR_ACCESS_LOST`, which every desktop switch produces. Each
tool's completion row now carries `desktop_switches` and `desktop_away_ms`.
The trial generator pins these builds (helper v15, reader qualification v15,
input qualification v24, observer qualification v7, cursor-guard qualification
v7). The switch's `enable` now generates the stimulus-free live variant by
default (`--stimulus` restores the trial measurement stimuli), so an enabled
desktop has no owned input window, repeated-input benchmark or shared
application beside it.

## Trial 23 (generation 30, admission v106, stimulus-free soak through the switch): 40 minutes, the provider child's fixed lifetime

Started 22:54:37Z by `npu-render desktop enable --monitor-seconds 7200
--initial-grant-seconds 1200` with the waiting tools (presenter v24, topology
control v15, observer v7, watchdog v4) and no stimulus, saver dismissals
scheduled for 23:44Z and 00:29Z. The desktop idled: 59 frame batches in 40
minutes, 62 frames relayed, presented and displayed, two lease renewals
(23:04:38Z and 23:33:38Z, 2340 s each) followed by the worker. No screen saver
appeared (Winlogon logged nothing); the dismissal tasks had nothing to do.

At 23:35:00Z the worker's provider child ended on its own: its last row is a
connect wait that hit its deadline (`client_departure`, win32 1460) followed by
`local_worker_finished` after 73 requests. The parent spawns the child with a
fixed grant (`"<pipe>" 65536 2400`) and no peers, so the child never runs the
lease-renewal thread that keeps the parent alive; 2400 s after admission the
child expires whatever the lease says. The parent then failed DWM's in-flight
batch (`provider_process_failure`, `retry_on_npu` false), reported "local NPU
worker is no longer running" and exited with code 1, and the bootstrap asserted
`NPU worker stopped before native frame evidence`. Every earlier run had either
ended before 2400 s or kept the child busy; trial 22 would have hit this wall
about a minute after the saver did.

DWM then crashed (Application event 1000 at 23:35:00.87Z, exception 0x889800b0,
dump `dwm.exe.20280.dmp`): the stack is `CLegacyRenderTarget::EnsureSwapChain`
→ `CDeviceManager::CreateNewDevice` → `CD3DDevice::CreateD3D11Device` → fail
fast. The bootstrap's teardown removed the indirect display and closed the
leases at 23:35:00.73Z, 150 ms earlier; DWM's device re-creation for the
changing topology reached our `OpenAdapter10` with no lease left and got
`DXGI_ERROR_UNSUPPORTED`. Trial 21 won the same race by seconds. Recovery
passed (23:35:01Z to 23:35:15Z) and the switch's own state is consistent
(`admission completed, passed=false, cleanup_passed=true`).

Changes:

- `local_indexed_worker.cpp` / `packet_provider_process.hpp` (tree v47): the
  provider child receives its owner's pid and moves its deadline to now+grant
  every 20 s while the parent lives (`provider_lifetime_follows_owner`); the job
  object still ends it with the parent, so the grant only bounds an orphan.
- Generated bootstrap: after removing the indirect display, wait for the
  indirect host to end (10 s at most) and settle three more seconds before
  closing any lease (`teardown_settle_ms`), so the compositor's device
  re-creation always finds its lease.

What this does not fix yet: a worker that dies for any other reason still ends
the admission with DWM re-creating its devices during the teardown; the
driver's in-process CPU path for frame batches, which would keep the desktop
rendering through a worker loss the way WARP would, is the next robustness
layer and is not built.

## Trial 24 (generation 31, admission v107, tree v47, package v41, 660 s): ordinary applications admitted, the saver waited out

The first admission with the provider child following its parent, the resident
interactive broker (v8 `--serve`) and the own-desktop binding for interactive
clients, started 23:52:12Z by `npu-render desktop enable --monitor-seconds 660
--initial-grant-seconds 1200 --build native-tree-v47 --package
native-driver-package-v41 --version 0.0.1341.0`. Scheduled against it from the
console session: a lease-holding consumer (`nr_auto_interactive_consumer
--hold 200` at 23:54:16Z), a one-shot consumer, an ordinary default-adapter
probe, the screen saver through the system path at 23:56:37Z and its dismissal
at 23:58:06Z.

The admission passed and recovered cleanly: 66 frame batches, no DWM crash, no
dump, `teardown_settle_ms` 3,000. The broker admitted ten clients with zero
rejections and ran sixteen renewal rounds (five clients still live at the last
one). The worker served every admitted client's requests on the NPU with zero
failures, by client: DWM 110, `StartMenuExperienceHost` 154 (abi 8 and 24),
`explorer.exe` 142 (abi 8, 18, 22, 24), `TextInputHost` 12, a process that
exited before the survey 56, `ClickToDo` 1. The ordinary adapter probe's two
`D3D11CreateDevice(nullptr, HARDWARE)` requests returned S_OK at feature level
10.0 (`application_default_device_created` true; every earlier stimulus-free run
recorded 887a0004). The holding consumer kept its scope-3 lease for 200 s with
40 verifications, its deadline advancing from tick 132834234 to 133025140
(`renewed` true), and the hold spanned the saver: the own-desktop binding kept
an admitted client valid while the input desktop was away. The one-shot
consumer passed as before.

The presenter and the observer each logged one `desktop_departed` and one
`desktop_returned` (away 90,016 ms and 89,813 ms), the observer re-created its
duplication twice, and presentation resumed: 40 frames over 548 s with the
last one 1.5 s before the end. The live wrapper's only failure was the
stimulus-free validator's observer freshness bound: the physical panel had not
changed for 61 s before the end of an idle desktop, and the duplication
observer only sees changes, so its last owned observation missed the 60 s
window (`fresh independently observed owned presenter absent`). Delivery
itself passed every other assertion. The window for the stimulus-free variant
is now 300 s; the presenter's own 60 s freshness bound stays.

What this does and does not show: ordinary applications on the enabled desktop
now create devices and have their draws executed on the NPU worker, which is
the admission half of gate 6; whether their pixels are correct is not
qualified (the capture of this admission can be replayed against the host
reference), and the saver process itself did not appear among the admitted
clients, so WPF's path is still unknown. Priority 3 is not met either: no
admission has yet run beyond 40 minutes; the multi-hour soak on tree v47 is
the next run.

## Trial 25 (generation 32, admission v108, four-hour soak attempt on tree v47): 22 minutes, ended by the previous admission's recovery task

Started 00:09:43Z by `npu-render desktop enable --monitor-seconds 14000
--initial-grant-seconds 1200` on tree v47, package v41, IDD v3, with the
resident broker and saver dismissals scheduled every fifty minutes. It ran
cleanly: six ordinary clients admitted (the Start menu host, explorer,
TextInputHost among them), 32 broker renewal rounds, one lease renewal at
00:20:12Z followed by the worker, 39 frame batches, 37 frames presented, no
desktop switch, no crash.

At 00:32:07Z the scheduled task `T8-NpuRender-NativeDesktopRecovery-v107`, armed
by trial 24's admission 2370 s after its start and never unregistered, fired
under SYSTEM and ran trial 24's recovery script: it removed `ROOT\DISPLAY\0000`
and `ROOT\DISPLAY\0001` (the live admission's indirect display), deleted the
staged driver packages (oem24.inf, oem21.inf), restored the certificate stores
and reported `passed=false` ("owned-process contract mismatch", "existing
immutable trace archive"), which is also why it had not unregistered itself
after trial 24: the script removes its task only on a passing run. The indirect
host process ended with the device, the relay and the bootstrap asserted on
their pinned host, and the admission's own recovery then failed to take the
recovery mutex that the stale run was holding ("Access to the path
'Global\T8.NpuRender.NativeDesktopRecovery.v1' is denied"), so `cleanup_passed`
is false although the stale run had already removed everything.

Trial 24's own recovery had passed at 00:03:48Z through the wrapper; the task
registered at the admission's start is a separate object that only its own
passing run deletes. Any admission whose recovery reports an error therefore
leaves a loaded gun pointed at the next admission, and its boot trigger
survives restarts. The fix is a lifecycle, not a flag: a recovery task is
unregistered whenever its admission has completed, whatever its own run
reported; the recovery script refuses to touch devices and packages while a
newer admission is running; the admission wrapper and the switch's `enable`
sweep stale recovery tasks of completed admissions before starting. Delta was
cleaned by hand after this trial (both stale tasks removed, display and package
state verified).

## Trial 26 (generation 33, admission v109, tree v49, package v43, IDD v4): the worker replaced in flight

The first admission with worker failover on both sides, started 01:28:34Z by
`npu-render desktop enable --monitor-seconds 660 --initial-grant-seconds 1200`
on tree v49 (provider child following its parent, exchange failover with a 30 s
window, interactive clients bound to their own desktop), package v43 and the
IDD v4 that no longer requires a live worker. Scheduled against it: a
lease-holding consumer at +150 s, a one-shot consumer at +200 s, both worker
processes stopped by a task at +240 s (01:32:42.84Z), an ordinary adapter probe
at +300 s and another one-shot consumer at +330 s.

The failover worked. The bootstrap recorded one restart: worker lost at
01:32:42.88Z (exit code 0xffffffff), replacement pid 18056 ready after 975 ms,
the compositor's lease rewritten (deadline tick 139820015) with the indirect
host's left as it was (139607734), the resident broker restarted beside the
first one's evidence. DWM's driver trace shows `exchange_failover_started`
("native probe lease rejected" on a frame batch) followed by `exchange_failover`
to worker 18056 after 47 ms, then the expected `frame_batch_missing` (two
resources the fresh worker did not hold) and a full resend; the replacement
served sequences 39 to 41 for DWM, the last a 1.59 MB batch. DWM kept its
process and its device; the indirect host stayed; the adapter probe at +300 s
created ordinary devices on our adapter after the restart (S_OK, feature levels
11.0 and 10.0).

Two things ended the probe early and neither is the failover. The lease-holding
consumer exited at the worker stop with "native probe lease rejected at
lease-proof": the fixture verifies its cached admission directly instead of
going through the driver's exchange, so it has no failover of its own; the
one-shot consumer at +330 s found no publication because the admission had
already ended. It ended because the live wrapper threw at 01:32:43Z on
`Get-ScheduledTask` for its own presenter task ("The system cannot find the file
specified") in the same second the worker-stop task unregistered itself; the
wrapper then wrote the stop file, the bootstrap ended cleanly (31 frames,
`stopped_by_controller`), and recovery removed the device and packages. That
recovery, like every one since trial 24, reported "owned process contract
mismatch" because the resident broker's role was not in its allowed list, so it
did not pass and left its task armed. Changes: the live wrapper's task-state
queries retry; the recovery script accepts the resident broker as an owned
process, records `cleanup_complete` (no owned device present, no device or
package error) and unregisters its task on that as well as on a passing run or
a completed admission. The next probe stops the worker at +300 s of a 900 s
admission so the frames after the failover can be counted.

## Trial 27 (generation 34, admission v110, 900 s requested): the resend after failover is too large

Same pins as trial 26, the worker stopped at +300 s (02:10:29Z). The
replacement was ready in 762 ms, the compositor's lease rewritten, the broker
restarted, and DWM's driver trace shows `exchange_failover` to worker 9172 after
62 ms, then `frame_batch_missing` for the two resources the fresh worker did
not hold. The resend carried every missing resource in full: one request of
50,944,884 bytes. The worker's parent accepted it (`request_identity`,
sequence 39) and its provider child ended the pipe (`indexed pipe completion
failed: 233`, `provider_request_failed`), so the driver's retry failed, the
transport was poisoned and DWM's flush returned an error; DWM re-created its
devices, the indirect display's swap chain was unassigned (`consumer_exit`
887a0026, `unassigned`), the topology control saw its primary route change and
restored the topology, the live wrapper wrote the stop file, and the bootstrap
ended cleanly at 02:10:40Z. DWM kept its process. Recovery passed with
`cleanup_complete` true and `task_unregistered` true, the first run of the new
lifecycle; no stale task remained.

So the failover protocol holds and the limit is the resend: a worker that
starts mid-admission knows no resident resource, and the compositor's next
batch references the whole desktop's surfaces at once. The driver has to
re-upload them within the worker's request limit, in pieces, before the batch
that needs them.

## Trial 28 (generation 35, admission v111, tree v50, package v44): the upload-alone path works, the reply limit and a device-creation hole remain

Same probe as trial 27 on tree v50. Before the stop, the driver already sent a
21 MB desktop surface alone (`frame_batch_upload_alone`, 359 ms). At the worker
stop (02:29:52.9Z) the replacement was ready in 685 ms, DWM re-bound after
63 ms, four resources were missing, the two large ones (21.2 MB and 28.1 MB,
the 2880x1844 and 3904x1800 desktop surfaces) went alone and were accepted
(worker sequence 54, 28,109,144 bytes, success), and the command batch that
followed was 2.95 MB. The worker's provider child rejected that batch with the
generic packet error, not a decode-stage one, so the failure is outside the
batch decoder: the only remaining size rule is the 64 MB packet limit on the
reply, and a batch whose dirty rectangles cover the three desktop surfaces
writes back about 70 MB. The parent worker then ended on the provider failure
(exit 1), the bootstrap replaced it a second time (456 ms), and in the
460 ms between the parent's end and the replacement's readiness DWM created a
graphics device: `OpenAdapter10` constructs an `Admission` whose constructor
verifies with the worker required, the lease still named the dead worker, the
open returned `DXGI_ERROR_UNSUPPORTED`, and DWM failed fast at 02:30:06.2Z
(dump `dwm.exe.18608.dmp`, `CD3DDevice::CreateD3D11Device` again). Recovery
passed with `cleanup_complete` and unregistered its task.

Changes for tree v51: the admission constructor verifies without requiring the
worker (the exchange still requires it where it connects), the packet limit
rises to 256 MB so a whole-desktop writeback fits, and the executor names a
reply that would exceed the limit instead of failing the batch generically.

## Trial 29 (generation 36, admission v112, tree v51, package v45): DWM survives two worker losses; the fresh worker's missing-resource rule

Same probe on tree v51. The worker was stopped at 02:43:30.7Z; the replacement
was ready in 942 ms and DWM re-bound to it. The 21 MB and 28 MB desktop
surfaces went alone and were accepted; the command batch that followed
(4.5 MB, 7 shaders, 6 resources, 4 uploads, 8 commands) was rejected again with
the generic packet error, the replacement ended on that provider failure at
02:44:06.2Z, and the bootstrap replaced it a second time (392 ms). This time
DWM kept its device through both replacements: the admission constructor no
longer requires the worker, so the device DWM created in the gap succeeded.
The second replacement served the compositor (its first batches 638 KB in,
2.6 MB back), frames continued (50 observed against 28 at the stop), the
ordinary adapter probe created devices after the restarts, and the admission
ran to its end. Recovery passed and unregistered its task. The live wrapper's
only failure was its end-of-run provider-generation audit, which expects one
worker lineage.

The rejected packet, parsed offline, is consistent to the byte: 80-byte header,
48-byte batch header, seven shaders, six resource records, four uploads and
eight commands ending exactly at the packet's end, and every decode-stage
check passes. The rejection is therefore in execution, and the packet shows
why: its first upload is a partial image (x 512, 512x768) of resource 0, the
1024x768 volatile target. A fresh worker holds no base image for that target,
but the executor's missing-resource rule accepted a volatile resource with a
window as present, so the batch drew into an empty target and failed on a
generic bounds check instead of reporting the resource missing (which would
have made the driver resend the full image). Trial 27's 50.9 MB resend was the
same batch shape one step earlier. Changes for tree v52: a resource used as a
target needs its base image whatever windows it carries, and a rejection names
the execution stage it failed in, not only the decode stage.

## Trial 30 (generation 37, admission v113, tree v52, package v46, 900 s): the worker replaced in flight, the desktop continues

The same probe on tree v52. The worker was stopped at 03:07:02.2Z; the
replacement was ready after 921 ms, the compositor's lease was rewritten and
the broker restarted. This time the replacement stayed: no provider child
rejected a batch, so there was no second loss. DWM kept its process (the same
pid since 02:30Z) and its device; the driver's trace shows the failover, the
large surfaces travelling alone (20.7, 21.2 and 28.1 MB), the fresh worker
reporting the resources it lacked and the resends, then 31 batches flushed to
the replacement; the bootstrap observed 24 frames after the stop (57 in all)
with the last one 33 s before the admission's end, and the worker served 452
requests. The one-shot client admitted after the restart passed, and the
ordinary adapter probe created devices on our adapter after it. The
provider-generation audit passed on the final lineage (`worker_lineages` 2);
recovery passed with `cleanup_complete` and unregistered its task; no dump.

Two bookkeeping defects remained. The bootstrap reported `passed=false`
because the grants on the dead worker's process object could not be restored
(`AssertionError(6)`, an invalid handle); the failover controller now drops a
dead worker's grants when it replaces it. The lease-holding consumer fixture
exits at a worker stop because it verifies its cached admission directly rather
than through the driver's exchange, which is the fixture's limitation, not the
desktop's. With those understood, the priority-3 item "the admission survives
the loss of its worker" holds: a worker that dies is replaced within a second
and the compositor, its clients and the display continue.

## Trial 31 (generation 38, admission v114, tree v52): the first live `disable`

A four-hour soak was started at 03:21:43Z and ended by `npu-render desktop
disable` at 03:27:05Z after the priorities changed: functionality first,
extended runs later, once Delta is not needed for them. In its six minutes six
ordinary applications were admitted in the first minute and the admission
reported `passed=true`; `disable` wrote the stop file, the bootstrap ended
cleanly, the admission wrapper's recovery removed the device and packages and
unregistered the recovery task, and `disable`'s own recovery then found nothing
to do (its only "error" is that the task it meant to unregister was already
gone, which the script should record as success). `disable` returned
`passed=true` after 54 s. All four switch verbs have now run on Delta.

## Probe v115 (tree v52, 660 s, no stimulus): what ordinary applications cost on the NPU desktop

After trial 31 the priority moved to functionality. The GPU-desktop control
sampler that ran before this probe left no sample file (its process had gone
and no `engine-samples-gpu-control-*.jsonl` exists); that control is still owed.
The probe itself was a stimulus-free `enable` of 660 s with Notepad launched at
+90 s, a WPF window (PowerShell, PresentationFramework) at +120 s and Edge at
+150 s through interactive tasks. The resident broker admitted seven processes
in the first minute, among them Notepad (pid 16140); the WPF host and Edge
never asked our adapter for a device (WPF renders through D3D9Ex, Chromium
decided against a D3D10-level adapter), so they were composited from their own
software surfaces by DWM, which is what the GPU path would do with any software
renderer, not a loss of content.

The worker log (`worker.stdout.jsonl`, `request_identity` and roundtrip rows)
attributes the admission's requests to processes (pids resolved on Delta):

| Process | Frame batches (abi 24) | Per-draw requests | Per-draw shader, target | Per-draw p50 / sum |
| --- | --- | --- | --- | --- |
| Notepad | 1,435, p50 15 ms | 1,433 (abi 8) | 61ce21fb, 16x16 | 31 ms / 51.0 s |
| StartMenuExperienceHost | 79, p50 31 ms | 77 (abi 8) | 61ce21fb, 16x16 | 94 ms / 11.4 s |
| explorer | 79, p50 16 ms | 75 (abi 8) | 61ce21fb, 16x16 and 768x576 | 93 ms / 9.9 s |
| TextInputHost | 6 | 6 (abi 8) | 61ce21fb | 62 ms / 1.1 s |
| dwm | 1,641, p50 109 ms | 523 (abi 7/8/11) | blur blob (e3ebf67c) fallbacks | 46 to 203 ms / 224 s |

Every application frame was one frame batch followed by one per-draw request
of the same pixel shader, 61ce21fb. Decoding the captured request
(`request-00000007.bin` of trial 30, a 2.1 MB request for a 16x16 target)
explains it: the shader is WinUI's text path. The vertex stage scales integer
glyph coordinates (`itof`), the pixel stage converts the varying back
(`ftoi`), forms `y * stride + ((x + offset) >> 3)` with `iadd`, `ishr` and
`imad`, fetches that element of a 512 KB R8_UINT buffer with `ld` (a glyph
coverage index: 84 of 524,288 bytes were non-zero), and fetches the colour at
(index, row) of a 256x80 B8G8R8A8 palette with a second `ld`. The frame kernel
had no `ld`, so the lowering rejected the pair ("pixel opcode") and the
driver's batcher rejected the buffer view before that ("texture view"); the
per-draw route then shipped the whole buffer and palette (2 to 4 MB) and ran 56
inference calls for 256 pixels. DWM's own cost in this probe is the blur
program (blob 10 then, blob 12 now) behind the Notepad window's backdrop: 523
fallbacks.

### Bundle v19, tree v55: the text program runs on the kernel

The kernel now lowers `ftoi`, `ishl`, `ishr`, `ushr`, `imad` and `ld`
(commits `feat(kernel): fetch texels with ld` and `feat(batch): fetched byte
buffers travel as texture bindings`). The planner bounds an `ld` by the int32
range of its address (the captured draw plans to 20 buffer elements and one
256-texel palette row; an unknown axis takes the whole axis, never an unbounded
window, because `ld` returns zero outside the resource). The driver admits the
R8_UINT buffer view as a fetched texture (the buffer is already an 8192-wide
word surface in the allocation model, so its resident copy and uploads are the
ordinary ones) and the batch protocol's texture binding carries the view's
element range (version 2); the worker cuts one-row R32_UINT windows of
zero-extended bytes for the kernel. Buffer writes now settle an open batch and
bump the content version like texture writes.

The text program is in a blob of its own (blob 4) and the 10 to 12 instruction
programs moved to blob 5, so the partition has 13 blobs; blobs without a
fetching program compile with `NR_FRAME_NO_LD` because moviCompile keeps unused
out-of-line helpers and the new ones pushed two blobs over the code window.
Bundle v19 on Delta (`frame-blob*-qualify-212052`):

| Blob | Programs | Text bytes | Fixtures on the NPU |
| --- | --- | --- | --- |
| 0 to 3 | 6, 5, 4, 2 | 20,496 / 23,600 / 22,320 / 20,192 | 29, 33, 27, 15, all exact |
| 4 | 61ce21fb (text) | 16,512 | 10, all exact (includes the in-bounds fetch fixture) |
| 5 to 11 | 1, 2, 1, 2, 1, 1, 1 | 19,152 to 24,496 | 9 to 15 each, all exact |
| 12 | e3ebf67c (blur) | 28,912 | does not fit (unchanged) |

Host checks: pair 27 vertex 96/96 exact, pixel 61 exact and 35 within one byte
of 96 (the palette's UNORM unpack), none far. Tree v55 (13-blob ranges) builds
clean on Delta; package v47 (0.0.1347.0) registered as v95.

## Probe v116 (generation 40, tree v55, package v47, bundle v19, 660 s): the text program on the kernel

The same 660 s application probe on the new pins (registration v95 passed in
49 s; `enable` launched the admission at 04:24:52Z, Notepad at +90 s, the WPF
window at +120 s, Edge at +150 s). The bootstrap reported `passed=true`, the
resident broker admitted seven processes (StartMenuExperienceHost, ClickToDo,
explorer, Notepad, TextInputHost and the two probes), no request failed, no
route disagreement, zero kernel escapes over 1,176 batches and 11,103 commands.

| Process | Frame batches (abi 24) | Per-draw requests | Per-draw shaders |
| --- | --- | --- | --- |
| Notepad | 11, p50 31 ms | 17 (abi 7) | 37b4d2f1 (13, explorer's large-texture program), e881b135 (4, 512x512, not captured) |
| StartMenuExperienceHost | 4 | 1 (abi 8) | 61ce21fb at 768x576 |
| explorer | 34, p50 47 ms | 0 | |
| TextInputHost | 1 | 0 | |
| dwm | 1,239, p50 110 ms, p90 437 ms | 1,116 | see below |

The text program ran on the kernel: the worker listed 61ce21fb as a `kernel`
command of blob 4 in every batch that carried it (82 commands by the midpoint),
and the per-draw requests of it fell from 1,591 in probe v115 (Notepad, Start,
explorer, TextInputHost) to one, a 768x576 target whose planned windows did not
fit the span budget. The per-frame cost of a WinUI application is now its frame
batch alone (Notepad p50 31 ms, explorer 47 ms). Notepad issued far fewer frames
than in v115 (28 requests against 2,868), which is consistent with the caret no
longer forcing a per-draw flush per blink but was not measured on screen; the
probe records neither focus nor the caret.

The compositor is now the cost. Its 1,116 per-draw requests (abi 7 p50 63 ms,
abi 11 p50 110 ms, 266 s of worker time in 660 s) and the 599 `legacy_draw`
flushes they force are the Mica backdrop of the Notepad window, and the DWM
trace names the two causes:

| `draw_not_batched` reason | Draws | Programs |
| --- | --- | --- |
| not a compiled inventory program | 477 | 1bf9cbd1 (180), 1cb9ebf7 (87), d9523e84 (86), 7f53ef7d (82) |
| span budget | ~420 | 595296ba sampling a 768x503 window (82) or a 970x390 window of a 1024x1024 texture (41), d8e0b497 with two 768x576 textures (123), 590x800 textures with 200 to 250 by 672 windows (85) |
| blur blob (e3ebf67c) absent | 404 fallbacks inside batches | the one program still over the code window |

The first group is captured now: the eight per-draw programs of probe v115
(d9523e84, 1bf9cbd1, 649fb489, 7f53ef7d, 8c7afc07, e5b2adfb, 1cb9ebf7 and
explorer's 37b4d2f1) were extracted from the admission's request recording,
use only instructions the kernel has (sample, divide, min, max, dot, compare,
select, mad), and are eligible; all eight match the SM4 reference on the host
(vertex 96/96 exact, pixel none far). The partition now deals a range's programs
round-robin over `splits` blobs so the 26 to 64 instruction Mica programs get a
blob each (17 blobs). Bundle v20 on Delta (`frame-blob*-qualify-213731`): blobs
0 to 15 fit (16,512 to 24,496 bytes) and pass all their fixtures on the NPU with
zero mismatches; blob 16 (blur) still does not fit. The second group, minified
textures whose per-tile windows are most of the texture, needs a different
window strategy (sparse taps or a mip chain) and stays on the per-draw route.

## Probe v117 (generation 41, tree v56, package v48, bundle v20, 660 s): the Mica programs on the kernel, and why DWM recreates its device

Same probe, new pins (registration v96 passed; two Notepad instances were on
the desktop this time, the one from v115 and the newly launched one). No request
failed, no route disagreement, zero kernel escapes over 1,799 batches and 22,562
commands. The compositor's `not a compiled inventory program` per-draws fell
from 477 to 5 (two new programs, 520a2995 and f5df7ac6, three and two draws):
1bf9cbd1 alone ran 973 times as a `kernel` command of blob 9. What remained on
the compositor's per-draw route is the span-budget family only (595296ba at
1024x768 and 64x64, d8e0b497 at 1024x768: 1,457 requests, p50 47 to 93 ms) plus
780 blur fallbacks inside batches. DWM's batches moved to p50 78 ms (v116: 110).

The bootstrap reported `passed=false`: its process observer found the
compositor gone at the end. `dwm.exe.8604.dmp` was written at 04:52:17Z, five
seconds after the admission's stop file, with `0x8898008d` ("No HW rendering
device is available") raised as a fail-fast from
`CD2DBitmap::EnsureShaderResourceView` while DWM was creating a device on the
adapter that the teardown was removing. Why DWM was creating a device at that
moment is the finding of this probe, and it predates today's changes:

- The worker log shows DWM opening a new exchange (request sequence restarting
  at 1) 2 times in trial 30 (no application windows), 81 times in probe v115,
  66 in v116 and 135 in v117, every 5 to 9 seconds once an application window
  is open. Each new device re-uploads the desktop's surfaces (88
  `frame_batch_upload_alone` rows of about 20 MB in v117) and the indirect
  display's swapchain is reassigned each time (270 `assigned` / `consumer_exit
  887a0026` cycles).
- The DWM trace (the first 4 MB of the run) pairs every device creation with a
  rejected draw: 43 `reject draw 80070057` rows, 44 `device_created`. The
  rejected draw is captured in `draw-8604.bin`: 6 vertices onto a 2880x1800
  B8G8R8A8 target under a 14x80 scissor, a 6x2880 texture, pixel shader
  8f244fea writing two outputs (`o0 = sample(t0, v2)`, `o1 = v1.w`) with the
  blend state `SrcBlend = SRC1_ALPHA, DestBlend = INV_SRC1_ALPHA` for colour and
  alpha: dual-source blending, DWM's caption text. The driver's blend table had
  no SRC1 factors, so the per-draw route threw E_INVALIDARG; the runtime treats
  a failed draw as a lost device and DWM recreates it. The draw's pixels were
  lost either way.

The fix in progress adds blend mode 14 (`dual_source_over`: source times the
second output's alpha, destination times one minus it) to the frame kernel
(`nr_blend_pixel_dual`, host blend reference and the 15-mode check), lets the
lowering keep a pixel stage's second output, maps the SRC1 blend state in the
driver, adds the pair as inventory program 36 (host: vertex 96/96 exact, pixel
none far), and makes both ends drop rather than fail a dual-source draw that the
kernel cannot take (the driver traces `draw_dropped` and counts it, the worker
reports `frame_batch_unsupported`): a lost caption glyph is reported, a lost
device is not acceptable. Bundle v21 and tree v57 carry it; probe v118 will
tell whether the device churn and the teardown crash go with it.

## Probe v118 (generation 42, tree v57, package v49, bundle v21, 660 s): dual-source blending lands, a second cause of the device churn

Same probe on the dual-source build (registration v97). DWM was a new process
(pid 5568, restarted after v117's crash). The bootstrap passed; the live
harness reported "per-generation provider verification failed" because the
worker's provider child was replaced twice (see below). No `draw_dropped` row
appeared: the caption text pair batched wherever it was drawn, and the
compositor's blend-table rejection is gone. The kernel ran 917 of 1,182
commands in the first provider generation and 19,227 of 19,303 in the third
(the rest the blur fallback), zero escapes.

DWM still opened 100 exchanges in 660 s. The first 4 MB of its trace (the
trace is capped there, so later minutes are not recorded) pairs the 40 device
re-creations with 39 rejected draws and names a different cause:

- One `operation_exception` "physical NPU worker rejected indexed draw": the
  open batch's flush failed. The worker's provider child rejected the 22 MB
  batch at its shader stage ("invalid frame batch at shaders") and exited; the
  worker's parent started generation 2, which rejected the resent batch the
  same way, then generation 3. The recorded request (ticket 104) decodes on the
  host: shader record 8, a 46-word pixel program, carries the digest of
  e5b2adfb, a 235-word program. The driver's digest cache is keyed by the
  shader object's address and DWM creates and destroys over a thousand pixel
  shaders per admission, so a freed program's address was reused and the new
  program inherited the old digest. The lowered-program cache had the same key
  and could have handed a reused address a wrong lowering, a wrong pixel result
  rather than a rejected batch.
- 38 `reject draw_indexed 80070057` rows with no draw row before them: a
  `require` failing before the draw is recorded. The dual-source blend state on
  a program without a second output is one candidate (the new mapping required
  `o1`); the next build logs the failing require's module offset.

Both caches now hold a weak reference to their program and recompute when it
has gone, a dual-source state on a single-output program is dropped and
reported, and the worker's shader-stage rejection names the shader and the
check (tree v58). The remaining per-draw traffic was the span-budget family
(595296ba at 1024x768 and 64x64, d8e0b497 at 1024x768) plus d8e0b497 on a
2880x1800 target (200 requests, abi 23) that the capped trace does not explain.

## Probe v119 (generation 43, tree v58, package v50, bundle v21, 660 s): the rejected draw names its check

Tree v58 holds weak references in the driver's shader caches, drops a
dual-source draw on a single-output program, and logs the failing require's
module offset with each rejection. The bootstrap passed with nine processes
admitted (two new Notepad-launched helpers among them), one provider
generation (no batch was rejected), no new DWM dump, 25,529 of 25,914
commands on the kernel (794 blur fallbacks), zero escapes. DWM still opened
126 exchanges in 660 s, but every one of the 38 rejections in the trace now
reads `require_offset 2dbb5`, and the failed-draw capture shows the state:
the textured modulate program 595296ba on the 1024x768 desktop, source 16x16,
blend `SrcBlend INV_DEST_COLOR, DestBlend INV_SRC_ALPHA, SrcBlendAlpha ZERO,
DestBlendAlpha ONE`. The driver required identical colour and alpha factors
and threw. That state is blend mode 15 in the next build
(`invert_destination_over`: colour = source times one minus destination plus
destination times one minus alpha, alpha lane unchanged), checked against the
host reference and mapped by the driver; blob 1 splits in two so the
2-instruction programs keep room (18 blobs), and blobs of large programs leave
the mode out (they sit at the code window) with the worker routing such draws
to the reported drop.

## Probe v120 (generation 44, tree v59, package v51, bundle v22, 660 s): one device for the whole admission

The same probe on the invert-blend build (registration v99). The compositor
opened one exchange and created one device in 660 s; its trace holds no
rejection and no dropped draw. Requests fell accordingly (DWM 456 against
3,540 in v119; the desktop's surfaces were uploaded alone 10 times instead of
88) and the bootstrap observed 37 frames (4 to 15 in the earlier probes, when
most of the admission went to re-uploading after each device loss). Eight
processes were admitted, no request failed, no route disagreed, zero kernel
escapes over 309 batches and 3,485 commands; the recovery removed the device
cleanly and DWM (pid 5568) is the same process it was before the probe.

Per-draw traffic that remains on the compositor's side: 187 span-budget draws
(595296ba at 1024x768 and 64x64, d8e0b497 at 1024x768), 14 draws of two
uncaptured programs (c7f4bc92, ca2803d7) and 58 blur fallbacks (blob 17); a
DWM batch still costs p50 94 ms with the Notepad window open, which is the
blur fallback and the span-budget draws interleaving with the batches.

The switch's pins move to tree v59, package v51 (0.0.1351.0) and bundle v22.
Open after this pass: the span-budget family (minified textures), the blur
program's code window, the two new programs, the GPU-desktop control sample,
and the 4 MB cap on the compositor trace that hid the later minutes of probes
v116 to v119.

### GPU-desktop control sample (2026-10-22 06:15Z to 06:25Z)

The engine sampler ran as an interactive task on the plain GPU desktop with no
admission and no stimulus for 600 s (`engine-samples-gpu-control-20261021-2315`,
271 samples; the first attempt from the SSH session at 03:30Z had left no file).
GPU 3D engine utilisation averaged 0 % with p99 0.01 %, total CPU 3.6 % (p99
7.6 %), DWM 0.55 % of all cores, the worker and indirect host absent. This is an
idle-desktop control: the trials' GPU 3D figures (p99 0.37 % in trial 12) were
sampled under the 30 Hz stimulus, so a stimulus-driven GPU-desktop control is
still owed before the two can be compared; the number above only shows that an
idle desktop leaves the 3D engine at zero on this machine.

## Bundle v23, carrier v2, tree v60: the code window doubled, span-budget draws grouped by triangle, five blobs

Three of probe v120's open items had one root each, found by replaying its
recorded per-draw requests through the planner on the host
(`bootstrap/requests/request-<ticket>.bin`, decoded with the lab's own
`indexed_exchange::decode`, planned with `plan_draw`, `fitting_tile` and
`adaptive_fit`).

### The code window: the carrier's text section can be grown

The blur program e3ebf67c (224 instructions, 10,930 bytes of row code in
bundle v22's blob 17 ELF) never fit the 24,496-byte window with the 17.5 KB
runtime beside it. The window is the `shave.text` section of the compiled
ScatterElementsUpdate blob, whose size the six `task.shave.range` descriptors
advertise (two rows each). `tools/delta/grow_frame_carrier.py` rewrites the
blob: the section grows from 24,624 to 48,176 bytes, every later section's file
offset shifts by the growth, the twelve range rows advertise 48,048 bytes of
code, and the OpenVINO NPU metadata trailer (plugin version, compiler string,
and the size of the ELF region it follows) is re-attached after the re-padded
ELF. The first attempt left that trailer in place and the plugin refused the
blob ("metadata version 0.0"); with the trailer rewritten the NPU imported it.

On Delta the v22 blob 17 ELF (28,912 bytes of text) patched into the grown
carrier passed all 9 of its fixtures bit-exact on the NPU
(`frame-blob17-qualify-48k`, 2026-10-22 13:29Z), so the grown window executes
code beyond the original 24,496 bytes. The 9 fixtures took 2,832 ms in one
request: the blur samples 18 times per pixel and this is the heaviest program in
the inventory; probe v121 measures what it costs inside live batches against
the 46 to 203 ms its per-draw route took.

`ptl_frame_carrier.py` now pins both carriers (v1 `06d57d6f`, v2 `aa8fe4fb`) and
reads the code window from the ELF; qualification and the worker accept either.

### Five blobs instead of eighteen

With 48,048 bytes per blob the inventory repartitions into five blobs by pixel
instruction count: 0 to 8 (22 programs, the compositor's common quads and
fills with the dual-source blend), 9 to 31 (9 programs with the text program's
`ld` runtime), 32 to 64 (4 programs: two Mica passes and the two programs
below), 65 to 181 (the three acrylic programs) and 182 and up (the blur alone).
Bundle v22's ELFs put the program code of those groups at 23, 20, 9.5, 16 and
11 KB beside a 15 to 20 KB runtime. Fewer blobs also means fewer kernel
requests: the worker runs consecutive draws of one target as one run only while
they share a blob, and probe v120's batches needed 8 requests for a median of 6
commands because neighbouring draws came from different blobs.

### Span-budget draws are hulls, not minification

Every draw the driver refused for the span budget in probe v120 (201 trace
rows, three shapes) is a draw of many small quads whose texels lie far apart in
one texture:

| Request | Draw | Triangles | Texture | Per-triangle windows | Hull |
| --- | --- | --- | --- | --- | --- |
| 100 | 595296ba, 141x31 px | 70 | 590x800 glyph atlas, point | 4 to 10 texels square | 201x672 |
| 96 | d8e0b497, 1024x37 px (taskbar) | 55 | two 768x576 atlases | 2x2 to 54x8 | 281x443 and 761x63 |
| 39 | 595296ba, 7x8 px (64x64 target) | 24 | 1536x1006 window image | 6x12 | 1516x996 |

The planner derived one window per texture per tile as the union of every
triangle's coordinate range, so two glyphs from opposite ends of the atlas
asked for the whole atlas, and bisecting the tile down to 8x8 pixels did not
help because the two glyphs stay in the same sub-tile. Nothing is minified: the
7x8-pixel draw is twelve 1x2-pixel quads reading the four corners of a window
image.

`frame_batch_planner.hpp` now partitions a draw's triangles into groups
(`group_triangles`): consecutive triangles join a group while the merged
windows (at most two per texture, `merge_rects`) stay within 25% plus 512 bytes
of the sum of the triangles' own windows, and a hull over distant rects starts
a new group. `tile_span_bytes` sums one command per group with that group's
vertices and windows; the worker packs each group as its own kernel command
with the same function, so driver admission and worker packing agree. Replayed
on the host, the three draws now fit the 128-pixel tile: 2, 3 and 3 groups
with 3.5, 16.8 and 17.1 KB of windows (spans of 88 to 137 KB). A group the
packer still cannot place after fitting no longer fails the batch: the run
restores its target and takes the per-draw routes, reported as
`frame_batch_span_budget_after_fit`.

### The compositor trace ring

The per-process UMD trace was capped at 4 MB and 16,384 rows, which hid the
later minutes of probes v116 to v119. It is now a ring of 16 MB files
(`process-<pid>.jsonl`, `.1.jsonl`, `.2.jsonl`, ...); the file three behind the
current one is deleted, so a process keeps at most 48 MB, and a reloaded UMD
continues from the highest file its process has. Readers glob
`process-*.jsonl` as the live validator already did.

### Two more compositor programs, one of them miscompiled on the device

c7f4bc92 (34 lowered instructions, one texture; 12 per-draw requests in v120)
and ca2803d7 (35 instructions, four textures, five varyings; 2 requests) were
captured from v120's recorded requests as pairs 37 and 38. The first bundle v23
build put both in blob 2 with 649fb489 and 8c7afc07: the blob compiled
(28,368 bytes) and the other three programs passed every fixture, but all six
fixtures of c7f4bc92 failed on the NPU, at -Oz, -Os, -O1 and -O2 alike, with
the host build of the same C matching the SM4 reference. The device writes the
green lane's value into red (`quad-c7f4bc92-rgba` expected 17,237,125 and got
237,237,125; the real-draw fixture got 199,199,199 for 238,222,56) with the
alpha lane right. The program unpremultiplies the texel, keys a clamp on its
luma (dp3, movc, lt, and) and blends with a constant; which of those constructs
the SHAVE compiler lowers wrongly is not yet isolated (the tapped-build bisect
that found the shuffle lowering fault is the tool for it). Pair 37 stays
captured but out of the inventory, so the draw keeps its per-draw route; bundle
v23 deploys ca2803d7 (pair 38) in blob 2.

### The GPU desktop under a 30 Hz stimulus (2026-10-22 13:43Z to 13:53Z)

The stimulus-driven control owed since probe v120: the engine sampler and a
WinForms window that toggles its background colour at 30 Hz in bursts of 300
updates every 30 s (20 bursts, 6,000 updates in 590 s) both ran as interactive
tasks on the plain GPU desktop with no admission
(`engine-samples-gpu-control-stimulus-20261022-0643`, 270 samples). The GPU 3D
engine averaged 0.18 % with p50 0.13 %, p90 0.35 %, p99 0.37 % and one 3.35 %
sample (DWM) as the window closed; total CPU 3.6 % (p99 9.6 %), DWM 0.58 % of
all cores, worker and indirect host absent, NPU 0 %. Trial 12's NPU-desktop
sample under its own 30 Hz stimulus showed the same p99 (0.37 %), so on this
machine the compositor's 3D-engine use for a 30 Hz window is already within a
fraction of a percent either way; the control gives the comparison its
baseline but does not show a measurable engine saving at this workload. This
stimulus is a GDI window toggling colours, not the trials' presenter relay, and
the figures are engine-utilisation samples, not frame timings.

## Probe v121 (generation 45, tree v60, package v52, bundle v23, 660 s): no span-budget draws, the blur on the kernel, half the kernel requests

The application probe (Notepad at +90 s, a WPF window at +120 s, Edge at
+150 s) on bundle v23 through carrier v2, registration v100
(`native-desktop-admission-v121`, 2026-10-22 14:08Z to 14:20Z). The worker's
startup self-test passed all 234 fixtures of the five blobs on the NPU (125,
58, 21, 21, 9; strict). The compositor created one device for the whole
admission, no draw was rejected or dropped, seven processes were admitted with
no request failure, the bootstrap observed 57 frames (37 in v120), DWM is the
same process it was before the probe, and the recovery removed the device.

What changed against probe v120, same 660 s and the same three applications:

| | v120 (tree v59, bundle v22) | v121 (tree v60, bundle v23) |
| --- | --- | --- |
| Compositor draws refused for the span budget | 201 | 0 |
| Compositor per-draw requests | 205 (595296ba, d8e0b497, c7f4bc92, ...) | 5 (two uncaptured programs, 520a2995 and 0b6602eb) |
| Blur draws inside batches | 58 per-draw fallbacks (blob 17 unavailable) | 50 kernel commands on blob 4, no fallback |
| Batches without a blur draw: kernel requests, kernel time (p50) | 7 requests, 26.9 ms | 4 requests, 18.7 ms |
| Batches with a blur draw (p50) | 17 batches: 22 commands, 36 requests, kernel 265 ms, host 1,239 ms | 12 batches: 153 commands, 103 requests, kernel 1,154 ms, host 1,448 ms |
| Compositor batch at the worker, queue wait excluded (p50 / p90) | 93 / 625 ms (255 batches, p90 5.3 MB) | 187 / 1,234 ms (96 batches, p90 20.7 MB) |
| Worker busy over the admission | 11 % (513 requests) | 6 % (171 requests) |
| Kernel escapes, tile failures, identity mismatches, route disagreements | 0 | 0 |

The partition did what it was meant to: an ordinary compositor batch now needs
four kernel requests instead of seven or eight and spends a third less time in
the kernel, and the whole admission kept the worker busy for 6 % of the time
instead of 11 %. The triangle groups removed every span-budget refusal, with
zero `frame_batch_span_budget_after_fit` rows: the planner's admission and the
worker's packing agreed on every draw.

The blur on the kernel is correct and expensive. Its 50 commands are small
strips (175x12, 175x7, 44x55 pixels of a Mica window's edges) but the program
costs tens of microseconds per pixel on one SHAVE (224 instructions, 18
bilinear taps with mirror addressing per pixel), so the batches that carry it,
which are also this probe's largest (p50 153 commands, 3.6 Mpx covered, Edge's
window uploads at 20 MB a batch), take 1.2 s of kernel time at the median and
4.1 s at p90, against 0.27 s of kernel plus 1.2 s of per-draw host time for
v120's blur batches. Those seconds queue the requests behind them (the five
per-draw requests waited 4 to 14 s), which is why the compositor's median batch
rose from 93 to 187 ms even as the typical batch got cheaper. The median is
also not like for like: v121's batches carried four times the bytes at p90.
The next performance lever is the blur program's per-pixel cost (the sampler
and the generated row code), or a policy that keeps a program whose kernel
cost exceeds its per-draw route on that route, reported as such; neither is
decided here.

Evidence on Delta: `vm\native-desktop-admission-v121` (bootstrap report,
worker and provider logs, the recorded requests, the compositor trace),
`vm\frame-blobs-v23\deploy\manifest.txt` (five blobs, stamp 065550),
`vm\frame-carrier-scatter-v2\carrier.blob`, `builds\native-tree-v60`,
`vm\native-driver-package-v52`. The switch's pins move to tree v60, package
v52 (0.0.1352.0) and bundle v23.
