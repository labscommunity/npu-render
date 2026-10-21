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
every passing blob ends at least 640 bytes short of it. The next experiment grows a
passing blob toward the window end with never-executed code and runs it through
both the tool and the worker self-test; if it starts failing, the last part of the
code window is not safe to execute from in the worker's environment and the usable
window is smaller than the 24,496 bytes assumed so far.
