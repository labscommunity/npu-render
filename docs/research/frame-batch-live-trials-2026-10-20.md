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
