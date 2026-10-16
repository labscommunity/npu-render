# Full desktop offload: acceptance and execution plan

Updated 2026-10-16. Target: Delta, Windows 11, Panther Lake. The repository stays
private. **Full desktop offload is unfinished.** Worker v9 passes all 972 retained
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
| 1. Routing | Record active physical and indirect paths before, during, and after admission; identify clone/extended configuration and actual rendering adapter. | Timestamped topology, native DWM NPU receipts, selected adapter, frame producer, exact restoration. | Automatic clone identified; v11 applies extension with the physical path preserved, produces a frame while extended, restores the clone exactly, and restores the final physical baseline. |
| 2. Continuous rendering | Keep the native renderer alive across changing windows and content; diagnose every failed draw; distinguish model preparation from rendering and transport. | Multiple changing frames, timestamps and pixel checks, sustained operation, no hidden fallback. | V36 passes with 1,833 acknowledged NPU operations, 167 exact frame deliveries spanning 2,076.05 seconds, and four fresh owned physical observations near the end. The controlled thirty-minute delivery gate passes for that earlier renderer. The timing-instrumented worker passes finite v42 integration with 48 matching submissions over 538.23 s; its thirty-minute run and production stress remain unqualified. V31 was a frozen-relay false positive and remains rejected. |
| 3. Physical presentation | Present new NPU frames on the physical output with distinct logical desktops and no recursive mirroring. | Live sequence-to-present correlation, output identity, measured presentation mode and residual GPU work. | V42 correlates 48 exact matching submissions and verifies blue/green/red on the owned physical output. Presenter: 96 GPU copy commands including validation readback; measurement observer: 142 additional copies. Residual GPU cost and a matched baseline remain unqualified. |
| 4. Interaction and recovery | Migrate the logical desktop, route pointer/keyboard/focus correctly, handle cursor/DPI/resolution, restore exact topology and windows. | Scripted interaction visible on physical output; emergency exit and owned-process failure recovery; ordinary login/lock path retained. | V42 passes relative motion, controlled mouse/keyboard/color changes, confinement, two reacquisitions and ordinary recovery. Deliberately stopping the relay in v34 triggers full owned cleanup in 25.75 seconds. Secure desktop, sleep/wake, full cursor fidelity and other faults remain open. |
| 5. Responsiveness | Optimize resource reuse, graph compilation, dispatch granularity, transfer volume and frame pacing. | Initial target: ordinary desktop interaction at 30 fps, p95 visible response ≤100 ms, 30-minute stable run. Report cold-start time separately. | Not met. Large cold graph compilation is reproduced at 89.1 seconds. Cached replay helps but does not meet usability. A fused four-sample kernel matches all 3.15 million components of the retained slow desktop draw in three comparisons. Six compiler-owned spans reduce complete warm draw time from about 9.2 s to 0.39 s, with 34 calls. The live v42 trial observes an 8.24 s controlled mouse response and 187.8 ms keyboard response. Causal timestamps attribute most of the mouse delay to frame production. Responsive interaction and wider shader/geometry coverage remain open. |
| 6. Application coverage | Admit and implement required graphics operations across browser, video and representative 3D workloads. Establish per-process adapter choice. | Capability matrix plus real hardware runs; every remaining iGPU operation accounted for. | A manually admitted default-adapter D3D11 fixture passes eight NPU operations. Unadmitted default device creation still fails in v41. A broker component passes normal-start lease, spoof rejection, timeout and cleanup checks. Registered D3D v32 then passes a normally started same-session fixture with eight exact NPU operations and cleanup. Registered v36 passes a normally started non-admin session-1 fixture, with automatic admission and eight NPU operations. Live v46 passes a bounded default-adapter fixture sharing DWM’s worker. Continuous admission, multiple applications, general API coverage and browser/video/game migration remain unqualified. |
| 7. Tahoma benefit | Run repeated baseline/offload trials under matched power, resolution and application load. | Report Tahoma throughput/latency, desktop frame times, per-engine GPU/NPU/CPU activity, memory and power; demonstrate GPU relief and no material Tahoma regression. | Earlier experiments regressed; native desktop A/B not yet run. |
| 8. Usable switch | Integrate qualified components, install/on/off/status, recovery, documentation and CI; resolve distribution signing requirements. | Repeated clean install and enable/disable cycles, sleep/wake and failures, end-to-end test on target hardware. | Research candidates isolated; no usable full-offload product. |

These are engineering acceptance targets, not measured results or an estimated
completion date. Gate 6 remains open if any requested application graphics class
still runs on the iGPU. Physical display scanout is an explicit remaining hardware
role, and any GPU rendering/copy cost in the bridge must be measured.

## Immediate work and decision gates

1. Retain the successful thirty-minute native delivery qualification and recovery
   as the integration baseline. Repeat it after meaningful renderer changes.
2. Preserve the exact six-span four-sample replay result and corrected capacity
   guard qualification. Reduce per-pixel work and broaden changing-image checks;
   independent imported-model overlap is excluded after real device loss.
3. Retain the qualified three-texture worker and live v39 result. Its complete
   replay attributes 4,996 calls and 22.29 seconds to the largest remaining shader
   group. Reduce shared geometry and dispatch costs across shader groups. The solid
   component clears five expensive draws but does not establish desktop usability.
   Repeat responsiveness and sustained stability checks after renderer changes.
4. Reproduce live v43's captured timeout and remove the remaining cold graph
   preparation from that rendering path. The reusable worker passes its 972-request
   replay, registered D3D and finite live v44. Preserve explicit routing declines
   and original evidence. Target the two dominant remaining shader groups from
   v44, then repeat finite and sustained desktop tests after rendering changes.
5. Integrate the qualified interactive admission component with the serialized
   desktop worker. Two ordinary non-elevated clients and five spoof rejections
   now pass, with exact process/token access-list restoration. Registered interactive
   graphics also passes eight real NPU operations. Live v46 qualifies a bounded
   default-adapter fixture sharing DWM's worker. Extend this to continuous admission,
   multiple clients, bounded renewal and revocation before browsers, video and
   representative 3D workloads.
6. Measure matched Tahoma baseline/offload performance and remaining GPU work.
   Finish install/on/off/recovery only after these functional and benefit gates.

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
