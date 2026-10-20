# npu-render

A switchable graphics runtime that moves supported rendering operations between
NPU, GPU, and CPU execution. The objective is a single control that frees GPU
resources for workloads such as Tahoma, with application and compositor adapters
providing the integration boundary.

**Status: private prototype.** The runtime implements deterministic premultiplied
RGBA composition and an explicit backend switch. The packaged runtime passed
GPU → NPU → CPU → GPU → NPU switching on Panther Lake. It supports CPU/GPU/NPU through
its first OpenVINO provider; other providers can implement the same contracts.

**Latest results, October 16:** the [packet-worker desktop trial](docs/validation/2026-10-16/native-response-packet-live-v1/README.md)
passes 1,682 acknowledged NPU operations, 103 exact physical submissions, forty
controlled inputs, the default-adapter test application and restoration, with
no provider fault or retry. Responsiveness still fails: both recent workers show
only seven verified animation updates during a ten-second 30 Hz test. The latest
warm sent-input-to-display p95 is 108 ms for mouse and 1,632 ms for keyboard.
The trace places 6.84 seconds inside child rendering with no compilation time.
Full desktop offload, sustained responsiveness, broad application coverage and
a Tahoma benefit remain unqualified.

**October 20 follow-up:** the completed [GPU baseline](docs/validation/2026-10-16/native-physical-gpu-live-v1/README.md)
reaches 29.9 verified animation updates per second through the same display bridge,
versus 0.7 on NPU. Its 550-second presentation and restoration pass, with zero
NPU rendering calls. This points to the NPU rendering path rather than the bridge
as the animation bottleneck. Tahoma throughput has not been measured in this setup.
The [host-memory experiment](docs/validation/2026-10-20/native-host-tensor-rejected-v1/README.md)
preserves all 3,888 replay images but increases aggregate request time by 7.50%.
Its live promotion is rejected before any desktop change.
The [balanced-geometry candidate](docs/validation/2026-10-20/native-balanced-geometry-v1/README.md)
preserves every image but improves aggregate request time by only 1.90%, below
the five-percent threshold for a new live trial.

Its [complete-image component comparison](docs/validation/2026-10-16/native-filled-program-comparison-v1/README.md)
preserves all 1,852 preceding NPU images. Across 1,389 warm pairs, aggregate draw
time falls from 76.77 to 59.58 seconds, with 121 slower candidate pairs. Existing
one-byte disagreements with the independent CPU reference remain unchanged.
This is a component measurement, not a desktop or Tahoma speedup.

The [point-sampling component](docs/validation/2026-10-16/native-point-program-v1/README.md)
preserves all 348 changed-image comparisons for 87 newly supported draws.
The [five-sample shader component](docs/validation/2026-10-16/native-complex-shader-v1/README.md)
matches an explicit FP32 reference on all 40 tested images. Five recorded images
differ from the old renderer; those numerical differences remain explicit.
The [next worker passes all 972 requests](docs/validation/2026-10-16/native-complex-worker-v1/README.md),
preserving 967 prior outputs and exactly matching the five explicit new-profile
references. The [combined rectangle worker](docs/validation/2026-10-16/native-point-rectangle-worker-v1/README.md)
also passes all 972 requests, preserving every preceding output. Its rectangle
policy passes 148 complete-image comparisons; 111 warm component pairs total
5.53 seconds versus 12.48 seconds on the old renderer. Live responsiveness and
a Tahoma benefit require separate qualification.
See the [current acceptance plan](docs/research/desktop-offload-plan.md).

**Full desktop offload is unfinished.** The current Windows/Panther Lake path
executes real DWM rendering on the NPU, delivers changing desktop frames to the
physical presenter, and supports a controlled mouse/keyboard sequence. The
[finite pointer trial](docs/validation/2026-10-14/native-quad-integration-v1/README.md)
passes foreground confinement recovery and restores the original desktop.
The same investigation compares thirteen expensive drawing operations in A/B/B/A
order: mean compile-plus-draw time drops from 121 seconds to 3.1 seconds, with
all 52 output hashes matching. This is a component result; desktop responsiveness
and a Tahoma benefit remain unqualified.

The optimized worker's [next live trial](docs/validation/2026-10-14/native-quad-live-v1/README.md)
delivers fourteen matching physical submissions but fails after a 93-second cold
compilation, client departure and later NPU device loss. Ordinary desktop recovery
passes. Reliable sustained rendering and model preparation are the immediate gates.

The [isolated provider now recovers from an actual NPU device loss](docs/validation/2026-10-15/native-device-loss-recovery-v1/README.md)
and completes a 3,291-request replay with every available reference hash matching.
Recovery takes about 63 seconds. Its subsequent native desktop trial passes
1,107 acknowledged NPU operations, 35 matching physical submissions, controlled
mouse/keyboard input and exact ordinary recovery. Mouse-to-visible response is
about 20.6 seconds and keyboard-to-visible response about 0.57 seconds in that
instrumented trial. Responsiveness, long-duration stability, application coverage
and a Tahoma benefit remain unqualified.

The [matched batching and model-sharing comparison](docs/validation/2026-10-15/native-combined-cost-v1/README.md)
preserves every available reference pixel across six complete recordings.
Larger batches alone are 8.5% slower; adding model sharing yields only a 3.1%
overall improvement. The combination also regresses on the separate expensive-draw
sample. It is not promoted as a responsiveness fix.

An [earlier short desktop trial](docs/validation/2026-10-15/native-duration-app-routing-v1/README.md)
completes 1,536 acknowledged NPU operations and exact restoration, but delivers
only 31 correlated physical submissions over 512 seconds. Another real device
loss takes 63 seconds to recover. Ordinary unleased applications fail to create
a default Direct3D device while the native adapter is primary; general application
admission is a separate outstanding requirement.

The goal remains a single switch that moves graphics work to the NPU and frees
iGPU capacity for Tahoma. The
[execution plan](docs/research/desktop-offload-plan.md) tracks stability,
responsiveness, application/browser/video coverage, remaining GPU work, matched
Tahoma performance, and installation/recovery. The switch in the packaged runtime
currently affects participating integrations. A composed NPU desktop frame does
not establish where every application's own rendering executes.

Earlier milestones below retain their original qualification scope and limitations.
The linked execution plan and newest validation reports describe current progress.

The [native registered-adapter trial](docs/validation/2026-10-13/native-windows-adapter-v1/README.md)
now passes six clears and two indexed draws through Delta's ordinary Direct3D 11
runtime, real Windows allocations and a separate NPU worker. Its driver needs
only Windows libraries; memory integrity stayed enabled. The temporary diagnostic
adapter, package and certificate trust were removed after verification. This
establishes native integration for the tested operations, not physical desktop
offload or a Tahoma speedup.

The [native account-boundary trial](docs/validation/2026-10-13/native-service-progress-v1/README.md)
passes ten NPU operations from a Local Service process after adding bounded
query/synchronization access to the owned controller and worker. The
[native routing candidate](docs/validation/2026-10-13/native-routing-progress-v1/README.md)
adds separate compositor/indirect-host admission and builds both driver packages;
both packages stage and remove cleanly. The subsequent desktop trials below
exercise actual native DWM routing; physical desktop migration remains unfinished.

The [native desktop controller candidate](docs/validation/2026-10-13/native-desktop-progress-v1/README.md)
revalidates the updated renderer through real D3D11, passes simultaneous NPU
consumers, and exercises automatic timed recovery. Its installation, compositor
admission, and cleanup sequence is assembled. The subsequent
[native desktop trials](docs/validation/2026-10-13/native-desktop-live-v1/README.md)
run after Delta is signed in and unlocked: actual DWM executes 13 NPU operations,
then 21 after fixing a large-surface transport limit. Both trials still exit
before delivering a desktop frame; automatic cleanup passes. A separate static
frame probe presents successfully, but its ETW trace fails to verify independent
flip. Full desktop offload and a Tahoma speedup remain unproven.

The [large texture investigation](docs/validation/2026-10-13/native-texture-address-v1/README.md)
captures the next failing native draw and reproduces incorrect large `Gather`
addresses on the NPU. Splitting source textures into bounded inputs fixes that
captured draw: CPU, NPU and the local transport return identical pixels, and
eight existing sampling regression runs pass. This repairs a specific desktop
blocker; it does not yet establish a stable native desktop.

An earlier [scripted desktop trial](docs/validation/2026-10-13/sole-display-v121/README.md)
renders the VM's desktop on Delta's physical NPU and passes desktop, Run,
Notepad, typing, maximize and close checks through the guest's sole indirect
display. It remains far too slow for normal use. Delta's physical desktop has
not migrated, and this path has not demonstrated a Tahoma benefit. The
[physical presentation plan](docs/research/physical-desktop-presentation.md)
describes the remaining integration. Current
[matched coverage replays](docs/validation/2026-10-13/compact-abba-v1/README.md)
preserve all 1,438 captured requests' pixels in four runs and reduce average
replay time by 5.5%. This remains an isolated optimization. The following results
describe earlier milestones and their individual limits.

The switch affects participating integrations. A restricted Weston adapter passes
Linux CPU headless/nested tests, and D3D12 shared buffers pass a real Panther Lake
NPU round trip on Windows. Native Linux NPU presentation, Windows DWM replacement,
arbitrary Direct3D/Vulkan games, and codecs remain unfinished. The earlier native
Tahoma investigation reduced NPU blend time by **33%**, but the optimized path
still made inference **4.7% slower** than a D3D12 shader in a five-round repeat
after Delta's power change restored increasing battery charge under load.
Isolation controls find a **3.3%** inference penalty from NPU blending even without
repeated uploads or presentation; adding comparable elapsed time with a host wait
does not reproduce it. The full-pipeline difference varies between experiments.
See the [power repeat and interference controls](docs/validation/2026-10-07/power-retest/README.md)
and [graph cost investigation](docs/validation/2026-10-07/slowdown/README.md).
Practical acceleration remains unproven.

A Windows experiment now hands composition of **three cooperating GDI windows**
from DWM to the NPU or GPU, hides the originals during delegation, and restores
them when switched back. All modes pass exact on-screen pixel checks. The private
bundle includes `run-delegate.cmd`: **1 DWM / 2 NPU / 3 GPU**, at a default 30
updates/s. Other applications and the outer Windows desktop stay on DWM. This is
a bounded handoff, not a system-wide switch. See the
[handoff and follow-up measurements](docs/validation/2026-10-07/desktop-delegate/README.md).

The actual handoff reduced sampled DWM activity, but NPU composition made Tahoma
**7.6% slower** than letting Windows compose these windows in five paired rounds.
The NPU tile-count and priority follow-ups also did not establish a reliable Tahoma
improvement. A direct adapter probe finds Delta's NPU through DXCore but confirms
that its installed driver rejects the tested Direct3D device-creation routes.
The [Windows integration assessment](docs/research/windows-compositor.md) describes
the additional renderer/driver work required for arbitrary existing applications.

An [autonomous retained-buffer follow-up](docs/validation/2026-10-08/retained-windows/README.md)
eliminates repeated foreground host uploads and batches placement into rectangle
copies. This reduces NPU frame processing from 15.1 to 11.7 ms. Tahoma throughput
improves 2.1% versus staged NPU in the paired median (four of five rounds), but
remains 3.1% below ordinary Windows composition in all five comparisons. Row-by-row
copies regress despite moving fewer bytes. Both retained variants remain explicit
experiments; the default is unchanged and a net Tahoma benefit remains unproven.

The [Direct3D command laboratory](native/windows/ddi/README.md) now loads our
process-local driver DLL through the ordinary D3D11 runtime and executes admitted
source-over draws on the NPU. It recognizes an exact shader pair and rejects
unsupported commands. Shared textures, presentation, a system graphics adapter,
and full desktop routing remain unimplemented; feature-level negotiation is not
conformance. The private Windows bundle includes this independently testable step;
[source and exact-package checks](docs/validation/2026-10-08/windows-ddi/README.md)
pass on Delta's CPU, GPU and NPU.

The [native texture follow-up](docs/validation/2026-10-08/texture-worker/README.md)
passes GPU-drawn input textures → separate NPU worker → native GPU shader sampling
at 2880×1800 in BGRA and RGBA. It uses no explicit CPU pixel uploads or output
copies. Creator release/reopen and parent/worker termination checks pass, followed
by successful recovery. The private bundle includes both finite texture probes.
This resolves a shared-resource boundary. Windows' general shader/state stream
and actual DWM selection remain unimplemented.

The subsequent [registered-driver laboratory](native/windows/registered_lab/README.md)
now loads through Windows' normal graphics stack in an isolated VM. Restricted
shader draws, image uploads, clears and process sharing pass 62 image checks with
clean removal in the [v31 test](docs/validation/2026-10-09/registered-draw-v31/report.json).
These registered draws explicitly use the CPU provider. The same draw callbacks
separately pass 16 checks on Delta's NPU using test allocation callbacks;
local installed-adapter NPU execution remains unproven. The
[v42 presentation test](docs/validation/2026-10-10/registered-window-v42/report.json)
now verifies 12 displayed frames through the registered adapter, including three
patterned CPU shader blends. It checks the actual window's screen pixels and
unloads cleanly. The original desktop and DWM remain active, and no Tahoma
speedup is established. The subsequent
[v43 NPU test](docs/validation/2026-10-10/registered-npu-window-v43/README.md)
executes three registered-driver draws on Delta’s physical NPU through an explicit
remote laboratory transport and verifies all 552,960 screen RGB bytes within one
byte. Local CPU/GPU draw-provider counts stay zero; CPU transfers, kernel copies
and the original desktop compositor remain. The
[indirect-display diagnostic](native/windows/indirect_lab/README.md) is the next
adapter-selection test, not an implemented desktop mode.

The isolated desktop work now captures DWM's real shader and draw state. An
[offline NPU replay](docs/validation/2026-10-10/dwm-draw-replay-npu-v53/report.json)
executes its first indexed draw through vertex shading, projection, triangle
coverage, pixel shading and BGRA output, matching all 786,432 output pixels.
This draw is an opaque uniform-color quad. The
[indexed driver callback](docs/validation/2026-10-10/indexed-ddi-npu-v60/report.json)
now passes two NPU draws through test allocation callbacks, including constant
updates and checked index offsets. An
[installed compositor test](docs/validation/2026-10-10/indexed-dwm-v61b/report.json)
then executed three real DWM draws on the physical NPU, with zero received or
displayed frames. General textured desktop rendering and indirect-display frame
delivery remain unfinished. The
first cold replay took 9.76 seconds including compilation. A subsequent
[rectangle specialization](docs/validation/2026-10-10/solid-rectangle-npu-v58/report.json)
preserves the exact image and reduces repeated draw time to 42–49 ms in the
diagnostic run. Its NPU pixel tests check 37.9 million components, including
fractional edges and unchanged background pixels. This still does not establish
interactive desktop performance or a Tahoma benefit. A
[native packet bridge](docs/validation/2026-10-10/indexed-stream-v65/report.json)
now returns exact 1024×768 NPU output from the physical machine to the disposable
VM. Warm roundtrips took 203 ms in the diagnostic run. The bridge uses a local
named pipe and a bounded authenticated stream; it is laboratory infrastructure,
and its successful packet tests do not establish desktop frame delivery. The
[next textured draw](docs/validation/2026-10-10/point-texture-npu-v71/report.json)
now has an exact NPU specialization for opaque, one-to-one point-sampled quads.
Its captured 1024×768 replay passes, as do changing-image and driver callback
tests. This is a checked subset, not general desktop rendering.

The [v113 installed trial](docs/validation/2026-10-12/sole-display-v113/report.json)
returns 440 acknowledged NPU results and nine frames, eight received while the
indirect display is the sole guest path. The complete desktop is visible, but
Run never appears on that path. Command-queue creation failures are followed by
NPU device loss. All four captured draws pass on CPU and, after restarting only
the NPU device, pass on NPU with identical output. Both custom guest drivers are
removed; Delta is not rebooted and its GPU is untouched. Runtime stability and
usable interaction remain unfinished. This trial does not establish a Tahoma gain.

A controlled [cache experiment](docs/validation/2026-10-12/capacity-experiments-v1/report.json)
reuses triangle models across render-target sizes and retains 24 complete
pipelines instead of 64. All 384 synthetic requests and 71 captured requests
preserve their expected pixels and coverage. The changing-target test improves
from about 25 seconds to 9 seconds; the changing-texture test is slower than its
warmed baseline (41 versus 21 seconds), because the smaller cache evicts models.
This does not establish desktop stability or a general performance gain.
The [v223 implementation](docs/validation/2026-10-12/surface-capacity-v223/report.json)
passes both clean 28-suite CPU builds and twelve NPU qualification stages,
including the four v113 captures and changing-target pixel checks. Its
[v114 installed trial](docs/validation/2026-10-12/sole-display-v114/report.json)
fails after 62 acknowledged NPU requests (17 prewarm, 45 live), with zero received
frames. An untextured 24×24 draw hits NPU device loss, followed by a worker access
violation. All three captured draws subsequently pass CPU and both NPU runtimes
with identical pixels after restarting the NPU device. The cache change has not
established desktop stability. Guest cleanup also requires restarting the owned
VM; both custom devices are then absent. Physical Delta is not rebooted. The
stream harness preserves live logs and completes output collection after the
worker crash. Ordered request capture is the next diagnostic step; full desktop
offload, usable interaction, and a Tahoma improvement remain unverified.

The [v115 diagnostic trial](docs/validation/2026-10-12/sole-display-v115/report.json)
returns 728 acknowledged NPU results and fifteen frames, eleven received while
the indirect display is the sole guest path. The desktop and Run are visible;
Run takes about 114 seconds to appear. Notepad starts but two of its draws exceed
our raster-work budget, and its window appears only after ordinary display
restoration. Its captured 756×406 draw reproduces the rejection on CPU: the
uniform batch selector unnecessarily shrinks 4,096 pixels to 8 to divide the
surface area exactly. This trial records no NPU device loss and removes both
guest drivers cleanly. Its recorder preserves
only 219 consecutive initial requests before reaching its storage cap, so the
earlier device-loss cause remains unresolved.

The [v224 partial-batch fix](docs/validation/2026-10-12/uniform-partial-v224/report.json)
keeps an efficient batch for arbitrary surface sizes and retains the existing
4,096-tile work limit. Both clean 28-suite CPU builds and all eleven qualification
stages pass. The Notepad capture now executes 426 tiles with exact oracle pixels
and coverage on CPU and both NPU runtimes. Its original driver packet also passes
with output identical to CPU. Odd surfaces, clipped tails, prior captures and
native draw callbacks pass. Its [v116 installed trial](docs/validation/2026-10-12/sole-display-v116/report.json)
then returns 792 acknowledged NPU results and thirteen sole-display frames. The
previous Notepad draw rejection does not recur. Its window border and blank
interior appear about 155 seconds after launch, but its complete UI and typing
remain unverified on the NPU path. Run appears after about 125 seconds. All 792
requests are preserved in order for replay; both guest drivers are removed
cleanly, with no NPU restart. Four untimestamped UMD rejection rows remain in the
archive alongside normal bounded cleanup. Full desktop offload is unfinished.
The Windows VM runs on the Mac; Delta provides the physical NPU. Delta's own
Windows desktop continues using its ordinary graphics path.

The extended [v117 trial](docs/validation/2026-10-12/sole-display-v117/report.json)
returns 830 acknowledged NPU results and nineteen frames, eighteen received after
sole-display confirmation. Run appears after 142 seconds; complete Notepad
interaction remains unverified. The trial exposes a harness error: the worker
was extended to thirty minutes while its relay retained a ten-minute cutoff.
Transport closure precedes the captured DWM failures; no NPU device loss is
logged. The native topology task later terminates without a restoration record.
Both guest devices are subsequently removed successfully and the ordinary
desktop is visually verified. The full request sequence and raw guest archive
are preserved. Full desktop offload remains unfinished.

With the relay lifetime corrected, the [v118 trial](docs/validation/2026-10-12/sole-display-v118/report.json)
returns 971 acknowledged NPU results and 25 sole-display frames. The complete
desktop, Run dialog and Notepad UI are visible. Typed text appears in Notepad's
tab title but is absent from its editor, so typing, maximize and close remain
unverified. The rejected editor shader uses unsigned right shift and bitwise OR.
The [v225 implementation](docs/validation/2026-10-12/packed-lookup-v225/report.json)
adds both instructions: 2,048 exact operation values and 4,096 captured-shader
pixels pass on CPU and both NPU runtimes, along with all 71 historical packets.
Both clean builds pass their 28 CPU suites. The installed desktop test is still
required. v118 restores the original topology and removes both guest devices
successfully; Run takes 118 seconds and the complete Notepad UI 193 seconds.

The [v119 installed trial](docs/validation/2026-10-12/sole-display-v119/report.json)
accepts that shader but exposes a second rejection: Notepad blends its glyph
mask with a constant text color using independent color-channel coverage.
The complete rejected draw is preserved and the unchanged CPU replay rejects
its blend state too. The trial returns 906 acknowledged NPU results and twenty
sole-display frames, then restores the ordinary display and removes both guest
devices. Editor text, maximize and close remain unverified on the NPU path.
A subsequent lookup test with 1,024 distinct palette colors passes every pixel
on the NPU; it does not resolve this separate blend rejection.

The [v229 blend fix](docs/validation/2026-10-12/component-coverage-v229/report.json)
replays that complete captured glyph draw with identical CPU/NPU pixels under
both runtime versions. Both clean builds pass 28 CPU suites; NPU checks cover
688,128 blend components per runtime, 71 historical packets, three ABI 15
packets, and 68 native driver draws. ABI 15 carries the text color as a runtime
input and supports independent channel coverage with preserved write masks.
The replay visibly renders the first editor glyph.

The [v120 installed trial](docs/validation/2026-10-13/sole-display-v120/report.json)
passes all six scripted visual checks on the NPU path: desktop, Run, Notepad,
typed editor text, maximize with retained text, and close back to the desktop.
It returns 1,438 acknowledged NPU results and 34 frames, 33 received while the
indirect display is the sole guest path. All six verified frames meet that gate.
The complete ordered request trace and 37 guest archive members are preserved
and hash checked. Ordinary topology is restored and both guest devices removed.
Application responses still take 83–207 seconds. This runs Windows in the test
VM; Delta's physical desktop remains on its ordinary graphics path. Usable speed,
general application coverage, physical desktop migration and a Tahoma benefit
remain unfinished. The [post-review](docs/validation/2026-10-13/sole-display-v120/post-review.json)
records two guest draw rejections during adapter removal after the visual checks,
and separates relay/transport timing from rendering measurements.

The [v121 live trial](docs/validation/2026-10-13/sole-display-v121/README.md)
repeats all six visual checks with the larger cache and point-crop reentry fix.
All 1,419 NPU requests are acknowledged, and restoration succeeds. Earlier
retained frames put application responses at roughly 67–253 seconds; delayed
manual review is recorded separately. The result preserves correctness but
remains far too slow for normal desktop use. Physical desktop offload and
Tahoma resource relief remain unproven.

The [ordered workload comparison](docs/validation/2026-10-13/queue-cache-v4/README.md)
replays all 1,438 requests with identical NPU pixels and execution receipts.
Retaining more live pipelines reduces elapsed time from 888.312 to 641.906 seconds
in the first comparison and model setup calls from 3,611 to 1,711. All setups
report disk-cache hits. Run order and warming effects remain uncontrolled; this
does not establish a live desktop or Tahoma speedup. The changes remain isolated
experiments. Three fresh-executor probes also confirm a missed fast path for
later full-screen copies, motivating a separate rendering fix.

The [crop re-entry fix](docs/validation/2026-10-13/point-reentry-v1/README.md)
now rechecks later draws after a partial copy. It passes 28 CPU suites,
3,576,128 independent NPU pixel checks, and the full ordered desktop trace.
Three affected copies use three NPU calls each instead of 1,538, preserving
every pixel and coverage count. Total calls fall from 61,822 to 57,221. A single
replay finishes 1.6% sooner; live desktop and Tahoma gains remain unproven.
The GPU control reproduces an existing projection rejection before the changed
path, so this qualification covers CPU and NPU only.

The v116 live draws expand into 26,995 provider calls. Their reported execution
intervals total 138 seconds, versus 329 seconds including pipeline setup and
other wrapper work. Of 1,921 model setup calls, 1,915 are disk-cache hits.
Retaining loaded pipelines and reducing provider-call count are optimization
targets; these sums do not measure pure hardware or compiler time. No Tahoma
performance gain is established by this desktop trial.

The preceding [v112 installed trial](docs/validation/2026-10-12/sole-display-v112/report.json)
returns 565 acknowledged NPU results and fourteen frames, thirteen received while
the indirect display is the sole guest path. The complete desktop and Run dialog
are visually verified, but Run takes 269 seconds to appear. Notepad becomes visible
only after the ordinary display is restored. A captured draw exposes a rejected
combination of bilinear sampling and shader alpha replacement. Both custom drivers
are removed afterward. Full desktop offload and usable interaction remain unfinished.

The preceding [v221 border-sampling qualification](docs/validation/2026-10-12/border-v221/report.json)
replays the rejected v111 DWM draw on CPU and physical NPU with identical complete
output and zero independent-oracle pixel error. Both clean builds pass 26 CPU
suites, and all 25 qualification stages pass, including 62 native NPU driver
draws, changing border colors and 63 historical desktop requests. The v112 trial
above uses this qualified implementation and reaches the next unsupported draw.

The [v222 alpha-replacement fix](docs/validation/2026-10-12/linear-opaque-v222/report.json)
then reproduces that exact draw on CPU and NPU with identical output. Both clean
builds pass 27 CPU suites; thirteen hardware qualification stages pass. The v113
installed trial above encounters a runtime failure during application interaction. A separate
[clamp optimization experiment](docs/validation/2026-10-12/clamp-diagnostic-v1-rejected/report.json)
failed NPU accuracy checks and was not adopted.

The preceding [v110 installed trial](docs/validation/2026-10-12/sole-display-v110/report.json)
returns 474 successful NPU worker results, with 473 acknowledged by the guest.
Nine frames arrive; eight arrive while the indirect display is the sole active
guest path. Desktop and Start pixels are visible, but Start takes 249 seconds,
and the last sole-display frame still shows Start after Escape. This is worse
than the prior trial's 88 seconds; the two live runs do not isolate the cause.
The automatic Start gate misses a 25-pixel displacement and adds 35 seconds
before manually verified Escape. Full desktop offload remains unfinished.

The corrected trace analysis identifies setup as a performance target: 179 of 260 live
triangle requests load or compile models, accounting for 297 seconds outside
draw timers. Of 1,074 model operations, 1,057 report disk-cache hits. The
[event-order audit](docs/validation/2026-10-12/sole-display-v111/v110-compile-event-audit.json)
corrects earlier counts that joined reused request identifiers. Retaining
ready executors and loading disk-cached models have different costs. Both
custom drivers are removed afterward. Three untimestamped UMD rejection rows
and one unacknowledged final worker result remain in the evidence; successful
worker computation alone is not proof of delivery. No Tahoma gain is established.

[Shader timing probes](docs/validation/2026-10-12/shader-profile-v2/report.json)
preserve all eight historical full-draw images and all 63 captured packet
outputs. In two warm full-screen draws, pixel-shader inference takes roughly
4.2 seconds and the whole draw about seven seconds. Setup and shader execution
both need work. A subsequent
[hardware profile](docs/validation/2026-10-12/shader-hardware-profile-v1/report.json)
preserves both historical images and reports roughly 2,000 Shave operations,
199 DMA operations and one DPU operation per captured pixel graph. Task durations
overlap and profiling adds overhead; these counts identify the execution path,
without establishing a faster implementation or Tahoma benefit.

The [qualified v217 cache candidate](docs/validation/2026-10-12/performance-qualified-v217/report.json)
retains 64 complete triangle pipelines, up from 16, while keeping independent
model ownership and eviction before replacement. Both runtime builds pass all
25 CPU suites, and all 19 native qualification stages pass. The captured replay
preserves exact output. The v111 installed trial above samples a peak process
private allocation of 782 MiB and working set of 2,217 MiB; device and driver
allocations are outside those counters. Start responds sooner than in v110,
but the live trials do not isolate the cache change from guest-state differences.

A [controlled pipeline cache experiment](docs/validation/2026-10-12/pipeline-cache-v3/report.json)
retains sixteen independently owned triangle pipelines instead of four. Its
63-packet replay takes 34.676 seconds, compared with 63.085 and 63.382 seconds
for the surrounding baseline runs. Every receipt and pixel remains exact; peak
private memory increases by about 72 MiB. The cache change is now integrated
with 8,192-lane batches for four exact captured compositor shaders on the pinned
OpenVINO runtime. [Complete draw qualification](docs/validation/2026-10-12/triangle-batches-v1/report.json)
preserves historical pixels and reduces warm full-draw time by 17–19% for two
captured shaders. Other programs and runtimes retain their previous scheduling.

The [integrated v216 renderer](docs/validation/2026-10-12/performance-qualified-v216/report.json)
passes both clean builds, 25 CPU suites per runtime, and 19 native qualification
stages. All 63 mixed captured packets retain exact pixels and coverage. A
[controlled warm comparison](docs/validation/2026-10-12/performance-ab-v1/report.json)
takes 36.940 seconds versus 39.429 and 39.743 seconds for the older sixteen-pipeline
build, a 6.7% reduction. The initial candidate replay took 95.257 seconds before
an identical repeat dropped to 37.730 seconds; first-use setup remains costly.
The subsequent v110 desktop trial above remains too slow for normal use.

The [partial-coverage correction](docs/validation/2026-10-12/crop-qualified-v212/report.json)
keeps valid point-sampled draws on the selected provider when they do not fit the
full-target crop optimization. The rejected 642×661 capture covers only 440 rows;
it now passes repeated NPU execution with exact coverage and zero pixel error.
The general path preserves the untouched background. Both clean runtime builds
pass 25 CPU suites. Native NPU qualification also passes 60 driver draws, eight
clears, a full–partial–full worker sequence and all 17 historical desktop packet
images. The subsequent v109 trial exercises this correction but does not establish a Tahoma gain.

The [model-sharing experiment](docs/validation/2026-10-11/model-reuse-v2/report.json)
passes 26 CPU suites and a small NPU lifetime/buffer test, but fails two of 42
captured NPU requests with device loss and an unavailable graph. The same
build with sharing disabled matches all baseline pixels and receipts. The
qualified renderer then recovers all 42 requests exactly without restarting
Delta. Model sharing remains outside the installed renderer; its failure cause
is not established.

The [three-texture fix](docs/validation/2026-10-11/three-texture-v199/report.json)
selects a qualified batch size for that exact captured shader. The unchanged
failed draw now completes on the NPU with exact coverage; CPU and NPU images
differ in 684 components by at most one byte. All 24 CPU suites and native NPU
regressions pass. The draw still takes about 55 seconds, so resolving compilation
does not establish usable interaction.

The [unfaded three-texture fix](docs/validation/2026-10-11/three-texture-base-v204/report.json)
handles the newly captured 1041-word variant with the same exact-bytecode batch
policy. Its full failed draw now passes CPU and NPU coverage and color oracles,
with at most one byte of output difference. All CPU suites and NPU regressions
pass; the NPU draw still takes 54 seconds.

The [new-runtime qualification](docs/validation/2026-10-11/runtime-qualified-v209/report.json)
enables 4096-pixel batches for the four captured compositor shaders on the pinned
OpenVINO 2026.4.1 build. Both complete three-texture draws now take about **8.87
seconds** on the NPU, with exact coverage and at most one byte of CPU/NPU color
difference. Both clean builds pass 25 CPU suites; 20 native NPU rendering checks,
58 driver draws, eight clears, cache reuse, legacy compatibility, and 17 exact
captured-packet comparisons also pass. These results qualify the next installed
experiment; they do not establish responsive full desktop rendering.

The runtime update required two independent corrections: integer bounds masks
for shader table lookups, and [cache isolation](docs/validation/2026-10-11/cache-isolation-v3/report.json).
Distinct RGBA and BGRA graphs receive the same built-in cache key in the tested
runtime, producing swapped colors even with a fresh cache. SHA-256 graph identities
separate those entries; all eight rectangle variants pass exact pixel checks both
when compiled and when reused from cache. Original blur and vertex-ID failures
also reproduce without caching, confirming the separate bounds correction.

The [blur capacity change](docs/validation/2026-10-11/tap-capacity-v201/report.json)
reduces two captured requests from 22.8 to 3.7 seconds and from 11.5 to 1.9 seconds
in repeated NPU execution, preserving every output pixel. It sizes the exact
shader's tap axis from its uniform loop bound; all sampling and arithmetic stay
on the NPU. All 24 CPU suites and native NPU regressions pass, including changing
bounds on the same executor. Cold capacity compilation adds roughly 12 seconds
in this experiment. The installed trial above includes this change.

[Rectangle row bounds](docs/validation/2026-10-11/linear-bounds-v202/report.json)
reduce a synthetically clipped linear update from 2.42 seconds to 131 milliseconds,
and a bottom point-sampled update from 2.01 seconds to 61 milliseconds, with all
output pixels unchanged. Empty intersections skip pixel inference entirely.
All 24 CPU suites and native NPU regressions pass. These packet measurements do
not establish desktop frame rate; installed interaction remains the next check.

[Deferred blur compilation](docs/validation/2026-10-11/deferred-blur-v203/report.json)
removes an initial graph that was immediately replaced on the first nine-tap
request. Six captured requests take 43.2 seconds in a fresh worker and empty cache,
down from 64.0 seconds, preserving every output pixel. Warm draw arithmetic is
unchanged. Four additional captured compositor requests also match the baseline
exactly and are ready for compilation before the next display switch.

[Triangle batching](docs/validation/2026-10-11/triangle-collation-v196/report.json)
reduces one captured request from 132 to 47 batches, preserving every pixel and
improving its warm draw time from about 6.0 to 2.65 seconds. Another request slows
by 6.6%. The [window batch fix](docs/validation/2026-10-11/packed-window-v198/report.json)
removes an obsolete target-area divisibility rule. Its previously failed draw
matches all 423,720 pixels on CPU and NPU and now executes in the installed trial.
These results establish neither full desktop offload nor a Tahoma benefit.

The [depth-state fix](docs/validation/2026-10-10/depthless-v194/report.json)
replays the earlier unchanged capture exactly on the NPU, including 700 covered
samples and the complete 512×512 target. All 24 CPU suites, 58 NPU driver draws
and eight NPU clears pass. Installed v100 also completes that draw on the NPU.

The [single-mip filter fix](docs/validation/2026-10-11/single-mip-v195/report.json)
now replays both new captures exactly on the NPU: 10,200 affected pixels in each
512×512 target. All 24 CPU suites and the native NPU driver checks pass. Installed
v101 progresses to a different failure in raster batch sizing.

The [snapped-boundary fix](docs/validation/2026-10-10/point-snap-v189/report.json)
retains exact raster boundaries while allowing bounded FP32 endpoint rounding.
Its captured 642×660 draw matches all 423,720 pixels on the NPU. All 23 CPU suites,
3,520,832 NPU point-draw components and 58 driver draws pass. A separate
[NPU clear graph](docs/validation/2026-10-10/clear-graph-v192/report.json) passes
151 million color components. Its
[driver integration](docs/validation/2026-10-10/color-clear-v193/report.json) now
passes 201.6 million NPU components, a real worker exchange, eight driver clears,
and all 24 CPU suites. Installed v99 uses the NPU clear path and accepts mapped
vertex-buffer binding while still rejecting reads before unmap. Stable interactive
desktop offload and a Tahoma benefit are unproven.

[Mirrored-texture qualification](docs/validation/2026-10-10/mirror-v174/report.json)
now replays that failed DWM blur draw on the NPU with all 8,192 output pixels
matching the CPU and scalar reference. MIRROR addressing crosses the driver,
versioned packet and NPU sampling paths. All 22 CPU suites, 196,608 native NPU
sample components, 58 driver draws and the existing NPU regressions pass.
The installed desktop trial is the next validation step.

The [loop-shader qualification](docs/validation/2026-10-10/loops-v165/report.json)
passes the captured 1,850-word mirrored filter on the NPU, including 33 scalar
comparisons, a scissored raster draw and native draw-packet delivery. General
admitted loops use a checked 16-iteration bound. The captured filter has an exact
bytecode specialization with up to 128 sample pairs and a verified 256-pixel
batch limit. A larger batch lost the NPU device during qualification; the failed
experiment and subsequent recovery check are retained. Twenty CPU suites and
57 NPU indexed DDI draws pass. The v93 installed trial reaches this
1,850-word shader, then rejects its MIRROR sampler state before NPU execution.

The [masking-shader qualification](docs/validation/2026-10-10/masks-v167/report.json)
adds bitwise AND and floating inequality with conservative predicate tracking.
The captured 223-word shader matches the scalar reference exactly on the NPU for
point and linear sampling. Its scissored raster and native draw packet also pass,
alongside 21 CPU suites and the NPU regression checks. The v92 installed trial
accepts its creation but still fails to deliver a complete desktop. A
[longer finite observation helper](docs/validation/2026-10-10/desktop-duration-v29/report.json)
now builds and passes ownership/readback contracts for the next installed test.

[NPU-computed triangle bounds](docs/validation/2026-10-10/triangle-bounds-v112/report.json)
now restrict tile dispatch while preserving exact output in eight replays of four
captured shell operations. Three warm draws drop from 8.27/1.75/4.43 seconds to
0.82/0.59/0.58 seconds. These are individual draw measurements; usable desktop
frame delivery remains the next validation boundary.

The [next Start menu capture](docs/validation/2026-10-10/sparse-budget-v116/report.json)
now passes on the NPU with exact CPU/reference pixels. Its 35 instances need 70
raster tiles; the old estimate rejected it as 7,560 tiles. Admission now uses
device-computed bounds and retains the 4,096-tile maximum. The v82 desktop
attempt failed to start tracing before installing any drivers; the disposable
VM was restarted and the [failure preserved](docs/validation/2026-10-10/sole-display-v82-preinstall/report.json).

An explicit [NPU model cache](docs/validation/2026-10-10/model-cache-v118/report.json)
now loads all six models in a captured draw from cache in a fresh process,
reducing compilation from 4.47 seconds to 0.16 seconds with exact pixels.
Rendering itself still takes seconds. The corrected collector also passes 100
snapshots during concurrent writes; both changes await desktop validation.

The [captured-window desktop experiment](native/windows/capture/README.md) now
presents an NPU-composed 2880×1800 desktop with input passing through to the
original applications, a disable/enable control, and an external hang watchdog.
It retains GPU capture/client rendering and fails equivalence to Windows' original
shadows/taskbar effects, so it is **not full desktop offload**. A separate
[shared allocation experiment](native/windows/wddm-memory.md) establishes real
cross-process NPU buffers and GPU consumption without a CPU upload of the result.
Neither is a system graphics adapter or evidence of a Tahoma improvement.

Texture sampling, affine transforms and specialized bilinear scaling now share the
native switch. The optimized NPU resize/compose API measured about 12 ms at 720p
and 27 ms at 1080p, including host memory work. These are off-screen operation
measurements; see [resize evidence](docs/validation/2026-10-06/resize/README.md).

A native Windows window now shares byte buffers between a D3D12 compute shader and
the NPU, follows the saved selection, and preserves its active backend on invalid
choices. With NPU driver 5540 active, GPU/NPU pixel checks pass at 720p and 1080p.
A 720p switching run and all twenty native inference trials completed on an active
desktop with every presentation returning `S_OK`; scanout timing remains unmeasured.
The private CI Windows bundle runs with `run.cmd`; press **1 / 2 / 3** to switch
GPU / NPU / CPU. See [native usage and limits](native/interop.md#native-windows-application).

The grouped-pixel NPU graph halves shared-buffer call time: about 5 ms at 720p and
9.5 ms at 1080p. A 1080p control run measured 13.9 ms median complete NPU processing
while occluded; the subsequent active-desktop evidence is at 720p. Exact 2× downscale/compose now
takes about 5 ms through the host API. See the
[layout and downscale measurements](docs/validation/2026-10-06/layout/README.md).

## Run it

Python 3.11–3.14 and a compatible installed device driver are required. The runtime
is pinned to OpenVINO 2026.1.0 to reproduce the first Panther Lake experiments.

```sh
uv sync --frozen
uv run npu-render devices
uv run npu-render stack
uv run npu-render select npu
uv run npu-render demo --output results/npu-frame.ppm
uv run npu-render select gpu
uv run npu-render demo --output results/gpu-frame.ppm
```

On a machine without an Intel NPU/GPU, use `select cpu`. A missing or unsupported
device returns an error. Selecting a backend compiles and checks a 16×16 fixture
before saving the selection; every new renderer separately qualifies its actual
shape. This selection is not a guarantee that arbitrary shapes will work.

For driver comparisons, `stack --expected-npu-driver 1005540` requires the exact
version reported by the loaded runtime. An installed driver package can differ
from the driver still executing before a required restart. Missing or mismatched
versions return failure with the captured report. The compiler-type property in
this inspection is a runtime preference; a compiled graph can resolve it further.

On Windows, `npu-render session --require-desktop` checks the current process's
session and desktop without changing either. The native contention benchmark
requires a connected `WinSta0\Default` desktop before inference warmup and around
each trial. This rejects session zero, a screen saver, and inaccessible desktops;
passing the check does not establish visible frames or physical scanout.

Selection precedence: explicit argument → `NPU_RENDER_BACKEND` → saved config →
GPU. `NPU_RENDER_CONFIG` or CLI `--config` selects a different config file.
`npu-render status` shows the effective selection. Existing applications call
`reload_selection()` on their control thread to apply saved changes.

```python
from npu_render import Composition, Renderer

renderer = Renderer(Composition(width=1920, height=1080, layers=1))
# Inputs: contiguous, writable NumPy FP16 arrays of shape (1, 4, 1080, 1920).
# Colors are premultiplied linear-sRGB RGBA; caller owns buffer synchronization.
frame = renderer.compose(background, [foreground])

renderer.switch("NPU")  # Prepare/qualify, then activate between frames.
frame = renderer.compose(background, [foreground])
print(frame.backend, frame.execution_devices)
```

Switching can take seconds and belongs on a control thread. Frames can continue
using the old backend during preparation. A failed switch leaves it active and
raises an error. Each returned frame owns its pixels. Inputs must remain unchanged
for the duration of a synchronous call. A graphics integration must handle format
conversion, fences, deadlines, damage, presentation, and device-loss policy.

## Evidence and direction

### Live switching and concurrent inference

```sh
uv sync --frozen --extra live
uv run npu-render live --backend GPU --seconds 60 --sequence NPU,CPU,GPU --switch-every 15
```

The window animates deterministic translucent surfaces. Press **1** for GPU,
**2** for NPU, **3** for CPU, **R** to reload the saved selection, or **Escape** to
exit. Compilation runs on a control thread while the previous backend keeps
rendering. Reports include actual execution devices, switch failures, each frame's
scene/composition/conversion/submission times, and skipped replay slots. The
software window still passes through the operating system compositor. Submission
timing is not physical scanout timing.

An optional C extension speeds up CPU color conversion; installation falls back to
NumPy if a compiler is unavailable. The report identifies the implementation.
This conversion is explicitly CPU work, not attributed to the NPU.

Against an existing Tahoma server running its model on the iGPU:

```sh
uv run npu-render-benchmark --endpoint http://127.0.0.1:8000/v1 \
  --model YOUR_MODEL --inference-device GPU --rounds 5 --requests 3 \
  --report results/contention.json
```

This rotates GPU/NPU/CPU/idle graphics conditions, keeps the prompt and animation
fixed, and saves raw per-frame and per-request results after every trial. Token
throughput uses server-reported usage; missing usage remains unknown. Text chunk
arrival times are recorded separately from engine token timings. The declared
inference device must be verified against server logs/telemetry. See the
[measurement protocol](docs/live-benchmark.md) before interpreting differences.

### Original device experiments

The original Panther Lake study measured median NPU graph calls of 4.34 ms for a
1080p one-foreground-layer blend and 5.55 ms for a 720p four-layer blend. The
four-layer 1080p case took 12.87 ms with tail samples exceeding a 60 Hz interval.
These are synthetic calls without presentation; CPU/GPU were faster on the same
four-layer graph. They establish a credible implementation path, not a Tahoma gain.

- [First-principles study and measured limitations](docs/research/first-principles.md)
- [Original probe, preserved for reproducibility](research/probe.py)
- [Original hardware evidence](docs/research/npu_graphics_delta_evidence.json)
- [Packaged runtime hardware validation](docs/validation/README.md)
- [Live rendering and Tahoma results](docs/validation/2026-10-06/README.md)
- [Driver activation and native Tahoma comparison](docs/validation/2026-10-07/README.md)
- [Native compositor adapter](native/README.md)
- [D3D12/NPU buffer interoperability](native/interop.md)
- [Operation coverage ledger](docs/coverage.md)
- [Architecture and operation boundary](docs/architecture.md)
- [Milestones toward the system-wide switch](docs/roadmap.md)

```sh
uv run python research/probe.py --device NPU --precision f16 --report results/probe.json
uv run python tools/validate_switch.py --backends GPU NPU CPU --report results/switch.json
```

## Development

**Every commit must follow Conventional Commits.** Merge, revert, and release
messages have no exemptions. Local hooks and CI enforce the policy. See
[CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).
GitHub server-side rules for this private repository are currently unavailable
under the account's plan; see [the enforcement status](docs/governance.md).

```sh
npm ci
npm run hooks:install
uv run pytest
uv run ruff check .
uv run ruff format --check .
```

Keep this repository private. There is no package-publication workflow.
