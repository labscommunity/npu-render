# Full desktop offload: acceptance and execution plan

Updated 2026-10-20. Target: Delta, Windows 11, Panther Lake. The repository stays
private. **Full desktop offload is unfinished.** The latest
[packet-worker v56 trial](../validation/2026-10-16/native-response-packet-live-v1/README.md)
passes 1,682 acknowledged NPU operations, 103 exact physical submissions, forty
controlled inputs, the ordinary test application and restoration. Warm
sent-input-to-display p95 is 107.663 ms for mouse and 1,632.388 ms for keyboard.
Nineteen warm samples per kind are a small distribution, with p95 equal to maximum.

Both v56 and the [preceding v55 trial](../validation/2026-10-16/native-repeated-input-live-v1/README.md)
verify only seven unique animation colors during the ten-second 30 Hz schedule,
with nine including drain. In v56, 210 overlapping draws contain 3,442 whole-request
provider calls; child execution occupies 6,839.935 ms with zero compilation time.
Only seven frames are published during that interval. The response-packet change
does not improve this animation result. Reducing rendering calls and shader cost
remains the immediate bottleneck. Matched GPU relief and Tahoma are unmeasured.

The [whole-request reuse analysis](../validation/2026-10-16/native-render-reuse-analysis-v1/README.md)
finds only about 3.7 seconds of prior child time on hypothetical bounded-cache hits
in a 110-second replay. No cache is implemented and no saved time is demonstrated.
A [read-only capacity query](../validation/2026-10-16/native-npu-capacity-v1/README.md)
reports three NPU tiles, matching the configured models. Turbo is disabled and
has not been benchmarked. Neither result supplies a responsiveness fix.

The [physical GPU baseline](../validation/2026-10-16/native-physical-gpu-live-v1/README.md)
now passes the same logical resolution, presenter, forty-input sequence and 30 Hz
animation: 299 unique updates appear within ten seconds (29.9/s), and all 300
including drain. The 550-second trial passes 519 exact submissions and restoration
with zero NPU rendering calls. This establishes that the bridge can carry the
requested animation rate; the NPU path remains responsible for the large deficit.
These are finite separate runs with different application preludes, not matched
Tahoma or resource-utilization measurements.

The [per-call fingerprint diagnostic](../validation/2026-10-16/native-tensor-fingerprint-v1/README.md)
preserves all 972 images but finds only 1.07–2.61 seconds of measured NPU waits on
hypothetical 64–256 MiB cache hits. Hashing is itself expensive. The completed
[host-tensor comparison](../validation/2026-10-20/native-host-tensor-rejected-v1/README.md)
preserves all 3,888 A/B/B/A images but increases aggregate parent time by 7.50%.
Its provider-recovery tests pass; its live promotion gate rejects the regression
before any desktop change. Cacheable input memory is not a rendering fix here.

The [balanced-geometry comparison](../validation/2026-10-20/native-balanced-geometry-v1/README.md)
preserves all 3,888 images and reduces aggregate request time by 1.90%, below the
five-percent live promotion threshold. The
[transport accounting](../validation/2026-10-20/native-animation-transport-analysis-v1/README.md)
also identifies 1,619.620 ms between acknowledgments and subsequent request
receipts in the retained ten-second animation. Client-side attribution remains
open; these gaps are not proven to be copying, admission checks or scheduling.

The [ordered-rectangle candidate](../validation/2026-10-20/native-quad-rectangle-v1/README.md)
also preserves every replay image. Its five targeted draws are 45.03% faster
across ten pairs, but aggregate time across the complete replay is unchanged.
The [transport-volume analysis](../validation/2026-10-20/native-transport-volume-v1/README.md)
counts at least 2.63 GB of logical surface traffic across both worker boundaries
for 209 complete requests during the retained ten-second animation. This is a
protocol count, not measured hardware bandwidth.

The [native client timing diagnostic](../validation/2026-10-20/native-client-timing-v1/README.md)
joins 1,482 measured DWM completions: 246.98 of 258.30 seconds in the sequential
client stages lie inside parent rendering execution. The instrumented animation
still delivers 0.7 verified updates/s, with 6.88 seconds in child rendering.
The finite run restores the desktop but fails a required texture-route coverage
check; it is diagnostic evidence, not a replacement passing desktop baseline.
Reducing shader work and NPU launches remains the priority. Retained resources
and command batching remain architectural investigations, not implemented gains.

The [combined clipping component](../validation/2026-10-20/native-resident-clip-component-v1/README.md)
now passes 144 exact full-span fixtures on the NPU, plus original-model checks
before and afterward. Its code fits the existing allocation. The
[integrated worker](../validation/2026-10-20/native-resident-clip-worker-v1/README.md)
preserves all 972 images and all 3,888 A/B/B/A images, and passes two targeted
recovery cases. Its 63 clipping draws improve by 71.69% across 126 pairs; total
replay time improves by 4.49%, below the five-percent promotion threshold.
The [standalone live gate](../validation/2026-10-20/native-resident-clip-live-v1/README.md)
rejects it before any desktop change. A combined candidate now brings together
clipping, balanced geometry and ordered rectangles, with separate correctness,
timing and recovery gates. This does not resolve the much larger frame-rate gap.
The rectangle candidate also passes
[two targeted provider-recovery scenarios](../validation/2026-10-20/native-quad-rectangle-recovery-v1/README.md);
its unchanged aggregate replay time still precludes a desktop speedup claim.

The preceding
[filled-program live v54 trial](../validation/2026-10-16/native-filled-program-live-v1/README.md)
passes 1,225 acknowledged NPU operations, 57 exact physical submissions, a shared
default-adapter application and restoration. Mouse readback completes in 2,171.722 ms
and keyboard readback in 94.671 ms; matching display counters occur at 2,076.730 ms
and 84.381 ms. The new routes execute 26 complex-shader draws and 151 rectangles.
No provider fault or retry occurs. These two events do not establish matched
improvement, p95, GPU relief or Tahoma benefit.

V54 also exposes an observer-analysis error: a valid acquisition began before
input and completed afterward. Filtering on acquisition start incorrectly skipped
that read. The corrected analysis selects completed owned pixel reads, retains
missing desktop-update timestamps explicitly, and passes eleven synthetic checks.
The failed analysis remains archived. The current mouse interval still includes
1,359.597 ms in child rendering and 681.913 ms outside it before publication.
Responsiveness remains the immediate gate, followed by sustained use,
application coverage and matched Tahoma performance.

The [response-packet worker comparison](../validation/2026-10-16/native-response-packet-comparison-v1/README.md)
preserves all 3,888 images in four A/B/B/A replays. Removing redundant parent-side
response decoding and encoding reduces time outside child execution by 23.73%,
but total parent request time falls only 2.03%, with 610 slower individual pairs.
This narrows the diagnosis: child rendering remains dominant. It does not yet
establish a desktop or Tahoma gain. The worker also passes
[two bounded provider-failure scenarios](../validation/2026-10-16/native-response-packet-recovery-v1/README.md).
The [repeated-input tools](../validation/2026-10-16/native-repeated-input-tools-v1/README.md)
are qualified for the next live response distribution and animation measurement.

The [compact generic-program component](../validation/2026-10-16/native-compact-program-v1/README.md)
passes 448 NPU fixtures within the existing code allocation. Profiling 586 batches
places 4,313.479 of 4,626.834 ms in NPU request waits, motivating a smaller payload
and larger batches. The [integrated worker](../validation/2026-10-16/native-compact-program-worker-v1/README.md)
preserves all 972 recorded outputs. The first selected comparison is retained as
[a rejected attempt](../validation/2026-10-16/native-compact-program-comparison-rejected-v1/README.md).
The subsequent [filled-batch comparison](../validation/2026-10-16/native-filled-program-comparison-v1/README.md)
preserves all 1,852 NPU images across 463 recorded draws. Its 1,389 warm pairs total
76.77 seconds before and 59.58 seconds after, with 121 slower pairs. Ninety-three
variants disagree with the CPU reference by at most one byte; every discrepancy
is unchanged between the two NPU implementations. This qualifies prior-output
preservation and component timing, not universal CPU-reference agreement or
matched desktop improvement.

Earlier milestones retain their original qualification scope. Worker v9 passes all 972 retained
requests and registered D3D11. Its [finite live v44 trial](../validation/2026-10-16/native-long-shader-live-v1/README.md)
passes 1,112 acknowledged NPU operations, 51 exact physical submissions, controlled
mouse/keyboard input and ordinary restoration. Memory integrity remains enabled.
The mouse-response frame takes 4,409.902 ms to publish and 4,541.028 ms to confirm
through independent physical-output readback. Keyboard confirmation takes 172.758 ms.
These two events do not qualify responsiveness or sustained use. Interactive
application coverage and a Tahoma benefit also remain open.

The subsequent [shared default-adapter trial v46](../validation/2026-10-16/native-shared-default-graphics-v1/README.md)
passes a normally started non-admin application on the same NPU worker as DWM:
eight correlated operations, followed by continued DWM rendering. The finite
desktop trial passes 1,209 operations, 53 matching submissions and restoration.
Mouse confirmation takes 6,603.050 ms and keyboard confirmation 237.390 ms;
these observations are not matched comparisons with v44. An unadmitted observer
still fails device creation. Continuous admission, multiple clients and broad
graphics coverage remain open.

The [point-sampling program extension](../validation/2026-10-16/native-point-program-v1/README.md)
passes 424 independent NPU fixtures and all 348 image variants for 87 newly admitted
recorded draws. Every original image is preserved. Summed warm component time falls
from 13.587 to 4.091 seconds with no slower pairs. Existing rectangle exclusions
remain intact. This component result requires worker replay and live qualification
before any desktop-performance conclusion.

The [five-sample shader component](../validation/2026-10-16/native-complex-shader-v1/README.md)
now matches an explicit FP32 reference on all 40 tested images. Five recorded
images differ from the old renderer; those numerical differences remain explicit.
Worker integration is recorded below; its component timing does not establish
live desktop responsiveness.

The [point-worker live v48 trial](../validation/2026-10-16/native-point-worker-live-v1/README.md)
now passes all 972 retained worker outputs, registered graphics, 1,223 live NPU
operations, 56 exact physical submissions and restoration. Its 99 point-program
draws confirm native use of the new route. Mouse physical-read completion is
3,087.583 ms and keyboard completion 251.850 ms. These remain two uncontrolled
cross-run observations, not a matched performance comparison. Rectangle and
five-sample shader work still contribute 1,248.446 ms before the mouse frame is
published. The failed screen-saver interruption is preserved separately; it caused
no native admission. Full offload and Tahoma benefit remain unqualified.

The [five-sample worker v12](../validation/2026-10-16/native-complex-worker-v1/README.md)
passes all 972 requests: 967 outputs unchanged from v11 and five exact explicit
FP32-profile outputs. Ten complex-shader draws share one model import. Forty
paired comparisons against the old NPU renderer preserve all independent FP32
reference outputs; thirty warm pairs sum to 34.591 seconds old and 15.641 seconds
new. Twenty-five variants differ from the old renderer by at most one byte per
component, as explicitly recorded. Registered graphics and live integration are
separate gates; this does not demonstrate desktop or Tahoma performance.

The [direct rectangle component](../validation/2026-10-16/native-point-rectangle-v1/README.md)
passes 233 independent NPU fixtures and 148 complete image variants for 37 legacy
point-rectangle draws. Both the direct-only and crop-preserving adapters retain
all original hashes. The comparisons expose distinct costs for plain and blended
copies; the subsequent route policy uses those semantics and row-batch count.
These are component findings, not proof of live responsiveness or GPU relief.

The [combined worker v14](../validation/2026-10-16/native-point-rectangle-worker-v1/README.md)
passes all 972 requests with every v12/v13 output preserved. Its rectangle policy
uses blend semantics and row count: 28 direct kernel draws and nine existing NPU
crops. All 148 image comparisons pass. Summed warm draw time falls from 12.483 to
5.525 seconds across 111 pairs, with 16 slower pairs. The worker also retains ten
complex-shader draws, one import per component, and zero provider failures.
Registered graphics and live integration remain separate from this replay gate.

The [integer rectangle candidate](../validation/2026-10-16/native-integer-rectangle-v1/README.md)
targets the measured color-request cost. It preserves all 233 device fixtures,
all 16,777,216 exhaustive source/background/alpha combinations, and 148 complete
images. An alternating-order comparison against the preceding rectangle kernel
reduces summed warm time from 5.864 to 4.517 seconds across 111 pairs. The candidate
now passes the worker and finite desktop gates below; this is not a Tahoma result.

The [integer worker v15](../validation/2026-10-16/native-integer-rectangle-worker-v1/README.md)
now passes 106 selected requests and all 972 retained requests. Every v14 output
is preserved, with no new profile exception, one rectangle import and no provider
failure. Its live v51 trial passes rendering and restoration but leaves desktop
responsiveness inadequate. The paired component gain does not establish a
whole-desktop improvement.

The [long-shader component](../validation/2026-10-16/native-long-shader-v1/README.md)
passes 120 independent NPU fixtures and exact complete-image comparisons for
the failed v43 request and all six earlier uses of the shader. [Worker v9](../validation/2026-10-16/native-long-shader-worker-v1/README.md)
preserves all 972 outputs from v8 and shares one long-shader import. Live v44
executes this path four times. The remaining mouse event includes 3,482.198 ms
inside child rendering requests before publication and 927.704 ms outside them.
Two shader groups, `595296ba…` and `c71638aa…`, account for 2,207.444 ms inside
those requests. They are the next measured rendering targets. This repair does
not complete the reusable compiler, thirty-minute qualification or application coverage.

The [live causal timing](../validation/2026-10-16/native-timing-live-v1/README.md)
places 8,167.77 ms of the mouse response before frame publication and 70.81 ms
between publication and observation. Eighty-one child rendering requests occupy
6,859.29 ms, including compilation and host orchestration. The masked-texture
path contributes only 143.08 ms, so another isolated improvement to that shader
would leave most of this delay intact. This attribution guides the reusable
shader path; it is one controlled event, not a latency distribution.

The [bounded program kernel](../validation/2026-10-16/native-program-kernel-v1/README.md)
passes 228 independent NPU fixtures. Its structural parser accepts 14 observed
programs representing 605 draws. Revised routing/resource constraints admit 381;
all pass [1,524 complete image comparisons](../validation/2026-10-16/native-program-comparison-v1/README.md).
Every original image is preserved. Four generated-image cases use an independently
qualified FP32 alpha-filter reference after attributing the old graph's FP16
precision reduction. Original failures remain recorded, without a tolerance.
Summed warm component time falls from 193.982 to 84.852 seconds, with 114 individual
comparisons slower. Worker replay and registered D3D integration pass, but the live trial fails on a
different shader path. These component results do not establish a responsive
desktop or a Tahoma benefit.

The [automatic graphics qualification](../validation/2026-10-16/native-automatic-graphics-v1/README.md)
passes eight real NPU operations from a normally started application through the
registered D3D11 runtime, with automatic lease creation and cleanup. This fixture
explicitly selects the adapter and shares session zero with its worker. It does
not qualify interactive default-adapter applications, existing-device migration,
browsers, video or games.

The [interactive admission component](../validation/2026-10-16/native-interactive-admission-v1/README.md)
passes normally started non-elevated clients across the session boundary,
identity-spoof rejection and exact access-list restoration. [Registered interactive
graphics v36](../validation/2026-10-16/native-interactive-graphics-v1/README.md) now
passes eight real NPU operations from session 1 with automatic scope-three admission
to the session-zero worker. Exact permissions, mapping, task and driver cleanup
pass. Its fixture explicitly selects the adapter. Live v46 subsequently qualifies
one normally started default-adapter fixture sharing DWM's worker. Continuous
admission, renewal and multiple applications remain open. The earlier component
archive retains an isolated reproduction of live v43's 17,057-node graph timeout.

The primary obstacle is responsiveness. A captured cold graph takes 89.1 seconds
to compile. The fully cached v35 worker also makes 42,760 provider calls across
1,253 completed operations; its slowest operation takes 9.60 seconds with 1,282
calls and no compilation. These are diagnostic whole-worker counters, not a
matched benchmark. Reducing dispatch count and rendering cost is essential in
addition to controlling compilation.
Our own kernels now perform the captured four-sample desktop draw across six
compiler-owned NPU spans in roughly 0.37 seconds, versus about 9.2 seconds on the
previous renderer, with exact output and 34 calls instead of 1,282. The guarded
worker passes all 972 recorded requests, registered Direct3D qualification and
native live v37. That finite live trial delivers 42 matching submissions over
523.93 seconds, exercises the specialization three times, and restores ordinary
desktop state. It remains slow: one controlled mouse response takes 27.21 seconds.
A broader revision now covers all 24 recorded uses of that four-sample shader,
with 96 exact component comparisons. Its integrated worker passes all 972 requests,
matches all 971 original references, and matches all 972 outputs from the prior
worker. A direct solid-rectangle kernel also matches five costly captured draws
using five calls each, down from 595–643, with roughly 13–23 ms component times.
The combined worker passes all 972 requests, registered Direct3D v26 and live v38.
V38 admits eleven four-sample and four solid draws, presents 46 matching frames,
and restores the machine. Its controlled mouse response takes 10.25 seconds and
keyboard response 127 ms. These two observations are not a latency distribution
or a matched comparison with v37. Other shader groups, model preparation and
normal application admission still require work.
Normal application admission and matched Tahoma A/B remain open. The earlier
thirty-minute delivery qualification belongs to the previous renderer.

The three-texture specialization now passes 36 complete comparisons against an
independently qualified, precision-preserving NPU graph. Its integrated worker
passes all 972 requests: 962 original references, nine explicitly qualified
coefficient corrections and one request without an original reference. An
independent audit confirms the other 963 outputs match the previous worker.
Registered Direct3D v27 and recovery pass. Live v39 also passes, including all
three custom paths, 52 matching submissions, controlled input and restoration.
Its observed mouse response is 11.59 seconds; responsiveness and this revision's
thirty-minute duration gate remain open.

Shared NPU geometry now passes 204 exact component comparisons across 51 draws
from two further shader groups. Summed component time falls by 23.69% and 14.76%
respectively, with some individual comparisons slower. Worker v5 then passes all
972 requests with pixels identical to v4, admits 460 shared geometry draws using
one model import, and passes registered Direct3D v28 plus restoration. These are
component and replay results. Live v40 subsequently passes, including 477 shared
geometry admissions, 49 matching physical submissions, controlled input and
restoration. Its observed mouse response is 5.79 seconds and keyboard response
236 ms; these two observations are not a matched performance comparison.
Evidence is retained in the
[shared geometry comparison](../validation/2026-10-16/native-general-geometry-v1/README.md)
and [worker qualification](../validation/2026-10-16/native-geometry-worker-v1/README.md),
with [live evidence](../validation/2026-10-16/native-geometry-live-v1/README.md).

The next two-phase masked-texture component passes all 41 captured draws across
164 changed-image comparisons. Summed component time falls from 90.843 to 33.105
seconds with exact pixels. Worker v6 passes all 972 requests, matches v5 in an
independent audit, and passes registered Direct3D v29 plus restoration. Its live
trial v41 also passes: 49 matching submissions, 25 masked draws, 435 shared
geometry draws, controlled input and restoration. Its observed mouse response
is 8.41 seconds and keyboard response 45.3 ms. It does not establish a responsive
desktop or a matched performance improvement. The
[component](../validation/2026-10-16/native-masked-rendering-v1/README.md) and
[worker evidence](../validation/2026-10-16/native-masked-worker-v1/README.md)
retain the checks, with [live v41 evidence](../validation/2026-10-16/native-masked-live-v1/README.md). The
[application admission investigation](automatic-application-admission.md)
addresses normally started applications, while the
[shader inventory](../validation/2026-10-16/native-shader-inventory-v1/README.md)
guides a more reusable instruction path. Neither establishes full coverage.

The end state is one switch that moves desktop graphics work to the NPU, retains
physical display scanout, supports normal interaction, and leaves the iGPU
available for Tahoma. Composition, application rendering, video processing, and
display scanout must be measured separately. An indirect display's preferred
renderer does not establish where every application renders.

## Ordered gates

| Gate | Work | Acceptance evidence | Current status |
| --- | --- | --- | --- |
| 1. Routing | Identify physical/indirect paths and the actual renderer before, during and after admission. | Timestamped topology, DWM NPU receipts, frame producer, exact restoration. | Finite v56 passes native routing and restoration. The physical output remains available. |
| 2. Continuous rendering | Render changing content without hidden fallback; handle failed draws and preparation. | Fresh frames, pixel checks, provider receipts, sustained operation. | V56 passes 1,682 acknowledged operations and 103 matching physical submissions. The earlier v36 thirty-minute result does not qualify the current renderer. |
| 3. Physical presentation | Deliver fresh NPU frames to the physical output without recursive mirroring; quantify remaining GPU work. | Sequence-to-present correlation, output identity, display statistics, residual GPU activity. | V56 correlates 103 exact submissions and controlled colors. The same bridge carries 29.9 animation updates/s in the GPU control. Residual GPU presentation/copy cost and resource relief remain unmeasured. |
| 4. Interaction and recovery | Route pointer, keyboard and focus; handle cursor, DPI and resolution; restore topology and windows. | Physical input observations, owned-process fault recovery, ordinary lock/login path retained. | V56 passes controlled input, confinement/reacquisition and ordinary restoration with memory integrity enabled. Current sleep/wake, secure-desktop and broader fault qualification remain open. |
| 5. Responsiveness | Reduce rendering, request, copy and scheduling costs; report cold start separately. | Ordinary animated desktop at 30 fps, p95 visible response ≤100 ms, thirty-minute stable run. | Not met. V56 warm sent-input-to-display p95 is 107.663 ms mouse and 1,632.388 ms keyboard. The NPU animation yields 0.7 verified updates/s versus 29.9 in the separate GPU control. Child rendering occupies 6.84 s of the NPU interval. No NPU desktop speedup is established. |
| 6. Application coverage | Admit ordinary applications and implement required browser, video and 3D operations; verify per-process renderer. | Capability matrix and hardware runs accounting for every remaining iGPU operation. | V56 passes a normally started non-admin default-adapter fixture sharing DWM's NPU worker. Continuous admission, renewal/revocation, multiple applications and broad browser/video/game coverage remain unqualified. |
| 7. Tahoma benefit | Repeat baseline/offload trials with matched power, resolution, content and work rate. | Tahoma throughput/latency, desktop frame times, per-engine activity, memory and power; measurable GPU relief without material regression. | Earlier experiments regressed. No native-desktop Tahoma A/B or GPU-relief result is established. |
| 8. Usable switch | Integrate qualified components into install/on/off/status/recovery; complete distribution and CI work. | Clean install and repeated enable/disable cycles, failures and target-hardware end-to-end tests. | Private research prototype. No usable full-offload product or complete application migration. |

These are engineering acceptance targets, not measured results or an estimated
completion date. Gate 6 remains open if any requested application graphics class
still runs on the iGPU. Physical display scanout is an explicit remaining hardware
role, and any GPU rendering/copy cost in the bridge must be measured.

## Immediate work and decision gates

1. Preserve the now-complete v55/v56 NPU trials and GPU control as the baseline.
   Both NPU trials deliver 0.7 verified animation updates/s; the GPU control
   delivers 29.9 through the same bridge. Keep input send, application receipt,
   publication, display counters and independent reads distinct.
2. Retain plugin-allocated tensors. The host-tensor experiment preserves images
   but is 7.50% slower across 1,944 paired requests. Its recovery tests pass and
   its desktop promotion correctly stops. For subsequent candidates require
   at least five percent aggregate request-time improvement and provider-fault
   recovery before repeating registered graphics and the finite desktop trial.
3. Retain balanced geometry packing as a small component result: all 972 images
   and all 3,888 A/B/B/A images pass, but request time improves only 1.90%.
   It does not meet the five-percent live promotion threshold. Do not
   infer physical tile utilization or desktop throughput from partition counts.
   Whole-request and per-call cache estimates are too small to justify treating
   caching as the principal fix. Continue reducing measured shader and launch
   costs, preserving complete images and tracking the remaining frame-time gap.
   The ordered multi-rectangle candidate preserves whole-batch geometry
   preflight, draw order and all 3,888 images. Its targeted calls fall from 380
   to 176 and targeted time by 45.03%; full-replay time remains unchanged.
   Do not promote this targeted measurement into a desktop speedup claim.
   A separate combined clipping kernel places small textures, coverage, sampling
   and blending in one NPU call per batch. All 144 target-device fixtures pass.
   Worker23 preserves all replay images and passes recovery, but its 4.49%
   aggregate gain misses live promotion despite 71.69% faster targeted draws.
   Measure the combined geometry/rectangle/clipping candidate under the same
   correctness, timing and recovery gates. Individual component gains are too
   small to explain a path to 30 fps by themselves; reducing submissions and
   redundant work across whole frames remains an architectural requirement.
   The [frame command batching contract](frame-command-batching.md) specifies
   resource versions, synchronization boundaries, recovery and staged NPU
   validation. It is a design, not an implemented performance result.
   Its first target link failed on missing `memset`; the next link was rejected
   for placing the helper before the kernel entry. A stale manifest in the third
   coordinator was corrected without changing the successfully built kernel.
   The first quad selection omitted all five relevant draws and is now corrected.
   Preserve these attempts. Native client timing now places about 95.6% of
   measured synchronous request time inside parent rendering, including CPU
   orchestration, child transport and NPU waits. This is not hardware utilization.
   Investigate
   retained resource versions and ordered batches if whole-surface round trips
   remain material; preserve map/present ordering and restart recovery.
4. After responsiveness passes, run the current renderer's thirty-minute soak,
   sleep/wake, secure-desktop and owned-failure recovery. Keep moving/resizing
   windows and broader desktop interaction separate from the simple color fixture.
5. Extend automatic admission to continuous leases, renewal/revocation and multiple
   ordinary clients. Establish a real browser/video/3D capability matrix. Account
   for every operation still using the iGPU; a successful DWM trial does not prove
   application migration.
6. Measure native-desktop GPU relief and Tahoma with matched display geometry,
   content, work rate, power and model settings. Keep this benefit gate separate
   from API coverage, and do not infer it from NPU execution or faster components.
7. Integrate the passing components into the user-facing switch, installer,
   status and recovery paths, then repeat the complete target-hardware acceptance
   sequence. Preserve all rejected trials and keep the repository private.

The [sustained delivery evidence](../validation/2026-10-15/native-long-kernel-v1/README.md)
qualifies controlled thirty-minute delivery and ordinary restoration. The
[direct rectangle](../validation/2026-10-15/native-direct-rectangle-v1/README.md) and
[larger source/batch tests](../validation/2026-10-15/native-wide-rectangle-v1/README.md)
qualify custom rendering components. The
[direct comparison and vector checks](../validation/2026-10-15/native-rectangle-comparison-v1/README.md)
retain the exact-comparison failures and corrected vector component pass.
The [captured four-sample replay](../validation/2026-10-15/native-four-tap-rendering-v1/README.md)
now establishes exact output and a roughly 4.72× warm draw improvement for one
retained request. The [source-window investigation](../validation/2026-10-15/native-four-tap-window-v1/README.md)
qualifies explicit sampling bounds and the corrected full-source capacity guard,
but does not improve that draw time. No live provider change or Tahoma benefit
follows yet.
The [six-span qualification](../validation/2026-10-15/native-six-span-rendering-v1/README.md)
now establishes approximately 24× improvement for that request, with exact output.
The [scheduling investigation](../validation/2026-10-15/native-four-tap-scheduling-v1/README.md)
retains the unsuccessful Gather layouts and concurrency failure/recovery.
The [guarded integration checks](../validation/2026-10-15/native-six-span-integration-v1/README.md)
and [complete replay and live trial](../validation/2026-10-15/native-six-span-live-v1/README.md)
now pass. They also identify remaining multi-second draws and a slow controlled
mouse response. They do not establish normal application migration or Tahoma benefit.
The [broader geometry checks](../validation/2026-10-15/native-four-tap-general-v1/README.md)
and [integrated worker replay](../validation/2026-10-16/native-general-worker-v1/README.md)
now cover all 24 observed uses of the four-sample shader. The
[normalization experiments](../validation/2026-10-15/native-byte-normalization-v1/README.md)
are exact but do not resolve the overall performance gap.
The [solid-rectangle component](../validation/2026-10-16/native-solid-rendering-v1/README.md),
[combined worker replay](../validation/2026-10-16/native-combined-worker-v1/README.md)
and [combined live trial](../validation/2026-10-16/native-combined-live-v1/README.md)
now pass. The live result confirms integration and recovery, while the measured
10.25-second mouse response still fails the responsiveness goal.
The [two-stage texture/shader component](../validation/2026-10-16/native-three-texture-shader-v1/README.md)
passes its independent NPU fixtures. Its
[first recorded-draw comparison](../validation/2026-10-16/native-three-texture-integration-rejected-v1/README.md)
differs in 15 byte components and is retained as rejected. Subsequent
[arithmetic tracing](../validation/2026-10-16/native-three-texture-arithmetic-v1/README.md)
and the [coefficient investigation](../validation/2026-10-16/native-coefficient-precision-v1/README.md)
identify precision loss in the old graph's negative multiply-add expression.
Two equivalent rewrites pass 16,384 independent NPU components each. The
[corrected full-draw comparison](../validation/2026-10-16/native-three-texture-correction-v1/README.md)
and [coverage compaction](../validation/2026-10-16/native-three-texture-compaction-v1/README.md)
each pass all 36 comparisons. The
[integrated texture worker](../validation/2026-10-16/native-texture-worker-v1/README.md)
passes the complete replay, independent correction audit and registered Direct3D
qualification. Original output hashes remain separate. Historical equality to the
old renderer does not establish that this coefficient arithmetic was correct.
The [live texture-worker trial](../validation/2026-10-16/native-texture-live-v1/README.md)
passes native integration and recovery but remains far short of responsive use.
These are prerequisites toward the stated
goal, not substitutes for application migration or Tahoma improvement.

## Earlier experiments

The [retained-model investigation](../validation/2026-10-14/native-retained-desktop-v1/README.md)
clears both captured failures and establishes a six-minute changing-frame bridge.
This is a working diagnostic baseline, not usable desktop offload. Larger finite
caches still need safe handling of eventual model retirement.

The [copy and presentation investigation](../validation/2026-10-14/native-copy-presentation-v1/README.md)
qualifies point-copy rendering through three recorded streams, registered D3D11
and a six-minute native trial. Texture-upload reuse exhibits an intermittent
failure and stays excluded. Direct display statistics and independent controlled
screen-color reads now provide evidence beyond submission acceptance.

The [primary routing and alpha investigation](../validation/2026-10-14/native-primary-input-v1/README.md)
establishes primary migration and ordinary recovery, but identifies missing window
content before the input gate. A 90-second compilation exceeds a desktop client
deadline; a later fresh replay passes all 477 captured requests. New primary trials
still encounter 24–27 second compilations. An alpha-copy compiler failure is
reproduced and a revised integer graph passes enlarged CPU/NPU fixtures.

The [primary visibility investigation](../validation/2026-10-14/native-primary-visible-v1/README.md)
finds the blue window in an in-progress buffer before a 91.5-second compilation
exceeds the client deadline. The corrected alpha worker clears five complete
recorded streams and registered Direct3D qualification. V17 completes 828
acknowledged NPU operations and delivers a blue completed frame near the end of
the interaction window. A screen read detects blue, with documented blocking-read
and ownership-timing limitations. Exact ordinary recovery passes.

The [primary interaction investigation](../validation/2026-10-14/native-primary-interaction-v1/README.md)
records 1,652 acknowledged NPU operations, 41 native frames, and 38 matching
physical submissions during a longer trial. Blue and mouse-triggered green are
verified with ownership checked before and after physical reads. Bounded retries
recover actual frame-file sharing conflicts. The keyboard test incorrectly
requires a tag on a translated character; ordinary-desktop reproduction and its
correction are retained. A DXGI observer and elapsed-time topology deadline are
prepared for the next native qualification.

The [controlled native interaction run](../validation/2026-10-14/native-primary-e2e-v1/README.md)
passes the corrected blue/mouse/green/key/red sequence, with 1,465 acknowledged
NPU operations, 41 native frames, and 38 matching physical submissions. Exact
topology and ordinary window/cursor/foreground restoration pass. A static standard
arrow matches a CPU reference exactly. Visible response remains about 56 seconds;
this finite diagnostic is not a performance qualification or full desktop offload.

The [pointer and rendering-cost investigation](../validation/2026-10-14/native-pointer-cost-v1/README.md)
qualifies ordinary-desktop confinement, foreground-transition reacquisition and
independent recovery after owner termination. Native v22 receives relative motion
and a click but aborts on a confinement change; later NPU device loss after topology
rollback remains unresolved. An isolated quad candidate matches thirteen expensive
draws exactly on NPU, reducing their calls from 1,186/1,282 to 54/72. Its warm draw
times are 143–248 ms, without live integration or a matched worker comparison yet.

The [quad integration investigation](../validation/2026-10-14/native-quad-integration-v1/README.md)
passes finite native pointer recovery and qualifies the integrated candidate,
including geometry checks and explicit same-NPU fallback. Matched A/B/B/A replay
shows a 39.02× compile-plus-draw reduction for thirteen expensive operations with
all 52 output hashes matching. These are component timings, not desktop frame rate.

The [optimized live investigation](../validation/2026-10-14/native-quad-live-v1/README.md)
passes the 972-request rollback stream and registered-driver qualification, then
fails its native desktop trial after fourteen matching physical submissions.
One new model takes 93 seconds, exceeding the desktop client's deadline; a later
request loses the NPU device. The final failed request passes in isolation.
Larger crop-band candidates pass exact isolated CPU/NPU contracts and remain
outside the live path pending reliability qualification.

The [provider recovery investigation](../validation/2026-10-15/native-provider-recovery-v1/README.md)
reproduces device loss in a recorded stream without changing the desktop. A later
3,291-request diagnostic soak passes, so the fault remains intermittent. A new
isolated provider preserves exact outputs across worker termination and simulated
device loss, and proves child cleanup when the supervisor exits. This does not yet
qualify recovery from an actual device loss or native desktop use. A cached replay
reduces the formerly 93-second request to 1.3 seconds; general preparation remains
unfinished.

The [real device-loss and native integration qualification](../validation/2026-10-15/native-device-loss-recovery-v1/README.md)
completes 3,291 recorded requests despite one real device loss, without fallback.
The 62.9-second recovery is correct but too slow. Its trace reports zero lost
events. Registered v22 and native v27 pass; the latter restores topology, ordinary
windows and pointer state after 1,107 acknowledged NPU operations and 35 matching
physical submissions. Controlled input responses are 20.645 seconds and 0.567
seconds in an instrumented trial. Neither is a responsiveness qualification.

The [short duration and application-routing investigation](../validation/2026-10-15/native-duration-app-routing-v1/README.md)
qualifies the bounded duration components, incremental evidence collection,
notification-driven relay and registered v23. Live v28 completes 1,536
acknowledged NPU operations and exact ordinary restoration, but only 31 matching
physical submissions over 512 seconds. One actual device loss takes 63 seconds
to recover. The native renderer becomes adapter zero, and ordinary unleased
default Direct3D device creation fails. A separately leased default-adapter
application proof and a thirty-minute native trial are next; neither establishes
automatic admission for all applications. This remains a prototype with a large
responsiveness and compatibility gap, not a near-term usable switch.

The [independent batching and sharing checks](../validation/2026-10-15/native-batch-sharing-v1/README.md)
pass, and the [combined correctness and matched cost investigation](../validation/2026-10-15/native-combined-cost-v1/README.md)
is complete. All available references match across six full replays. Larger batches
alone increase aggregate preparation plus draw time by 8.54%; adding sharing brings
the combination to only 3.06% faster than baseline. The separate 65-draw sample
regresses 4.25%. The candidate misses the proposed 10% gate and is not promoted.

The [stage profile and scoped-upload investigation](../validation/2026-10-15/native-profile-scope-v1/README.md)
identifies the shared vertex/pixel shader executor as the largest measured
inference cost in sixteen recorded draws. Scoped texture uploads reduce matched
preparation plus drawing time by 18.76%, with all 96 outputs exact and no recovery.
The [complete-stream comparison](../validation/2026-10-15/native-full-texture-scope-v1/README.md)
now passes: scoped uploads plus memory model reuse reduce preparation and drawing
time by 14.52% in A/B/B/A order across 4,436 acknowledged requests, with all 4,432
available reference hashes exact and no provider faults. Native qualification
remains required before promotion. This combined comparison does not isolate
the contribution of either setting.
Reported binding bytes are not measured host-copy or device-bus traffic.

The [scoped native trial audit](../validation/2026-10-15/native-scoped-desktop-v1/README.md)
rejects a controller false positive. Live v31 delivers 31 matching physical frames
and completes 1,403 acknowledged NPU operations, but its frame relay exits on a
Windows file-replacement error. The controller continues showing the last frame
and incorrectly reports success. The derived qualification is false; the raw
reports are preserved. Most residual counter samples overlap the frozen display
and cannot quantify capacity freed for Tahoma. The next trial requires relay
health, successful relay termination and fresh independently observed delivery
near the end of its interval. Controlled visible responses remain 25–29 seconds.

Keeping uniform shader expressions scalar passes 9.44 million checked components
in each CPU/NPU baseline/candidate configuration, but the recorded candidate
differs at ticket 140. It remains excluded from desktop admission. A diagnostic
now reproduces the mismatch: CPU baseline/candidate match all 20,736,000 output
components, while scalar constant-buffer lookup changes three NPU components by
one byte each. The NPU baseline also differs from CPU in 141 components. This
narrows the investigation without qualifying the candidate or changing the pixel
acceptance contract.

The diagnostic relay and presenter also impose fixed 100 ms waits. Those waits
prevent a 30 fps bridge even if rendering becomes faster. An event-driven relay
and a presenter without the fixed post-present wait pass live v28 integration;
that trial remains very slow. The presenter still polls unchanged frames every
millisecond. A message-aware notification wait is prepared separately to remove
that host work, with no measured speed or power claim yet. Directory notifications are wake-up hints; frame
identity, completeness and monotonicity checks remain required.
[Microsoft: directory notifications](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstchangenotificationw),
[rearming notification waits](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextchangenotification).

The bounded forty-minute worker and UMD lease ceilings, incremental evidence
collection and matching recovery deadlines pass component and short native
integration. The first long attempt exposes an omitted consumer: the indirect
display host still rejects leases exceeding fifteen minutes. Its 38-minute
admission exits before rendering; ordinary recovery passes. A matching indirect
host contract and boundary tests are prepared for the retry. The long
trial requires advancing presentation timestamps and an independent physical-output
color observation after thirty minutes, followed by exact ordinary restoration.
Prepared code and parser checks do not satisfy the duration gate.

The [delivery repair investigation](../validation/2026-10-15/native-delivery-repair-v1/README.md)
now proves the corrected IDD accepts the longer lease, but the retry ends early.
The controller encounters a file-not-found error whose call site was not saved;
later the NPU worker times out. The captured request independently reproduces the
timeout: a 17,057-node graph needs 89.09 seconds of compilation. Cached repetition
completes in 1.62 seconds. Both failures and ordinary recovery are retained.
Continuous delivery therefore remains open, and shader preparation must stay
outside the ordinary frame deadline without concealing fallback or frozen frames.
Bounded atomic-publication retries now pass real Windows sharing tests; a stricter
validator rejects frozen delivery and missing late physical observation.

The same investigation establishes eight exact NPU operations from an owned
application using the ordinary default D3D11 adapter. That is progress on routing,
but admission still requires a manually issued finite lease. Automatic per-process
admission, lease renewal and representative application API coverage remain open.
An isolated shader/blending fusion experiment passes exact CPU/NPU pixel checks;
it is not part of the live renderer and has no qualified desktop speedup.

Continue with responsive sustained operation, application coverage, residual GPU
cost and matched Tahoma benefit. New applications' adapter selection must be
observed independently of DWM routing. Exact-output memoization covers only about
3% of recorded cost at 64 MiB and remains lower priority. Component passes do not
complete the overall goal.

## Architecture constraints from primary documentation

Windows lets an indirect display driver request a rendering adapter; the OS can
select a different one, and the assignment callback reports the actual adapter.
The selected adapter therefore must be checked on each swapchain assignment.
[Microsoft: IddCxAdapterSetRenderAdapter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter).

The desktop renderer and an application's adapter selection are distinct parts of
the routing decision. A composed frame on the NPU alone cannot demonstrate that
the application's own graphics workload moved.
[Microsoft: IddCx 1.4 rendering adapter preferences](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/iddcx1.4-updates-for-console-and-remote-idds).

Active source/target paths and their associated modes provide the topology evidence.
Virtual-aware path queries require interpreting the corresponding union members
correctly; the qualified topology helper uses virtual-aware queries and interprets source and target indices accordingly.
[Microsoft: QueryDisplayConfig](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-querydisplayconfig),
[Microsoft: mode/path relationships](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/relationship-of-mode-information-to-path-information).

## Evidence discipline

Preserve failed trials as well as successes. Record source and binary hashes and
use a new transaction identity for each trial. Keep captured desktop pixels,
models, caches, private addresses, and credentials out of the repository. Commit
only reviewable code and text evidence, using Conventional Commits.

The [coverage and interpolation kernel](../validation/2026-10-16/native-geometry-kernel-v1/README.md)
passes its independent NPU fixtures and 36 complete three-texture draw comparisons.
It remains isolated from the live worker while broader geometry and shader
coverage are being qualified.
