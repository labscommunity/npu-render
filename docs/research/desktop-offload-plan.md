# Full desktop offload: acceptance and execution plan

Updated 2026-10-15. Target: Delta, Windows 11, Panther Lake. The repository stays
private. **Full desktop offload is unfinished.** The latest qualified native trial
routes Windows desktop composition through the NPU, delivers 167 matching frames
over 2,076.05 seconds to the physical output, verifies mouse/keyboard color changes,
and restores the machine. Its 60-second freshness gate proves sustained delivery,
not usability. Normal application admission and a Tahoma benefit remain open.

The primary obstacle is responsiveness. A captured cold graph takes 89.1 seconds
to compile. The fully cached v35 worker also makes 42,760 provider calls across
1,253 completed operations; its slowest operation takes 9.60 seconds with 1,282
calls and no compilation. These are diagnostic whole-worker counters, not a
matched benchmark. Reducing dispatch count and rendering cost is essential in
addition to controlling compilation.
Our own kernels now perform full-source indexed reads and combined rectangle
rendering on Panther Lake's NPU. The larger rectangle test passes 12.6 million
components, including 1920×1080 source textures, shared-memory source buffers up
to 8 MiB, and 16,384 output pixels per call. Original-graph health checks pass
afterward. The controlled thirty-minute delivery gate also passes.
The first direct comparison with the current provider does not qualify a
replacement: scalar timing is mixed, and the old provider has some different
quantized pixels. The custom kernel matches its independent float32 oracle;
the CPU provider also differs near some rounding boundaries. The corrected vector
kernel passes the same 12.6-million-component suite after two retained failed
candidates. Numerical diagnosis, a fused implementation of the captured four-sample
shader and task-level timing are next, followed by live integration.
General application support and Tahoma A/B remain required before calling the
switch usable.

The end state is one switch that moves desktop graphics work to the NPU, retains
physical display scanout, supports normal interaction, and leaves the iGPU
available for Tahoma. Composition, application rendering, video processing, and
display scanout must be measured separately. An indirect display's preferred
renderer does not establish where every application renders.

## Ordered gates

| Gate | Work | Acceptance evidence | Current status |
| --- | --- | --- | --- |
| 1. Routing | Record active physical and indirect paths before, during, and after admission; identify clone/extended configuration and actual rendering adapter. | Timestamped topology, native DWM NPU receipts, selected adapter, frame producer, exact restoration. | Automatic clone identified; v11 applies extension with the physical path preserved, produces a frame while extended, restores the clone exactly, and restores the final physical baseline. |
| 2. Continuous rendering | Keep the native renderer alive across changing windows and content; diagnose every failed draw; distinguish model preparation from rendering and transport. | Multiple changing frames, timestamps and pixel checks, sustained operation, no hidden fallback. | V36 passes with 1,833 acknowledged NPU operations, 167 exact frame deliveries spanning 2,076.05 seconds, and four fresh owned physical observations near the end. The controlled thirty-minute delivery gate passes; production stress remains unqualified. V31 was a frozen-relay false positive and remains rejected. |
| 3. Physical presentation | Present new NPU frames on the physical output with distinct logical desktops and no recursive mirroring. | Live sequence-to-present correlation, output identity, measured presentation mode and residual GPU work. | V36 correlates 167 exact matching submissions and verifies blue/green/red on the owned physical output. Presenter: 334 GPU copy commands including validation readback; measurement observer: 530 additional copies. Residual GPU cost and a matched baseline remain unqualified. |
| 4. Interaction and recovery | Migrate the logical desktop, route pointer/keyboard/focus correctly, handle cursor/DPI/resolution, restore exact topology and windows. | Scripted interaction visible on physical output; emergency exit and owned-process failure recovery; ordinary login/lock path retained. | V36 passes relative motion, controlled mouse/keyboard/color changes, confinement/reacquisition and ordinary recovery. Deliberately stopping the relay in v34 triggers full owned cleanup in 25.75 seconds. Secure desktop, sleep/wake, full cursor fidelity and other faults remain open. |
| 5. Responsiveness | Optimize resource reuse, graph compilation, dispatch granularity, transfer volume and frame pacing. | Initial target: ordinary desktop interaction at 30 fps, p95 visible response ≤100 ms, 30-minute stable run. Report cold-start time separately. | Not met. Large cold graph compilation is reproduced at 89.1 seconds. Cached replay helps but does not meet usability. Direct combined rectangle kernels pass 12.6 million components with full-source shared-memory access. The scalar provider comparison has mixed timing and pixel differences; no replacement is qualified. The captured expensive shader also requires four samples and derivative handling. End-to-end timing and live integration remain open. |
| 6. Application coverage | Admit and implement required graphics operations across browser, video and representative 3D workloads. Establish per-process adapter choice. | Capability matrix plus real hardware runs; every remaining iGPU operation accounted for. | A manually admitted default-adapter D3D11 fixture passes eight NPU operations. Unadmitted default device creation still fails in v36. Automatic admission, API coverage, browser/video/game migration remain unqualified. |
| 7. Tahoma benefit | Run repeated baseline/offload trials under matched power, resolution and application load. | Report Tahoma throughput/latency, desktop frame times, per-engine GPU/NPU/CPU activity, memory and power; demonstrate GPU relief and no material Tahoma regression. | Earlier experiments regressed; native desktop A/B not yet run. |
| 8. Usable switch | Integrate qualified components, install/on/off/status, recovery, documentation and CI; resolve distribution signing requirements. | Repeated clean install and enable/disable cycles, sleep/wake and failures, end-to-end test on target hardware. | Research candidates isolated; no usable full-offload product. |

These are engineering acceptance targets, not measured results or an estimated
completion date. Gate 6 remains open if any requested application graphics class
still runs on the iGPU. Physical display scanout is an explicit remaining hardware
role, and any GPU rendering/copy cost in the bridge must be measured.

## Immediate work and decision gates

1. Retain the successful thirty-minute native delivery qualification and recovery
   as the integration baseline. Repeat it after meaningful renderer changes.
2. Preserve the qualified full-source rectangle path. Diagnose compiler/vector
   conversion and existing-provider rounding differences without changing the
   acceptance criteria to hide failures. Implement the actual captured four-sample
   shader, preserving derivative, coverage, blending and source-span semantics.
3. Compare the fused implementation against the current provider with identical pixels and
   complete transfer/preparation/commit timing. Promote only an exact, measured
   improvement, then repeat live responsiveness and stability checks.
4. Add automatic application admission and the missing graphics operations;
   establish per-process rendering devices under representative real workloads.
5. Measure matched Tahoma baseline/offload performance and remaining GPU work.
   Finish install/on/off/recovery only after these functional and benefit gates.

The [sustained delivery evidence](../validation/2026-10-15/native-long-kernel-v1/README.md)
qualifies controlled thirty-minute delivery and ordinary restoration. The
[direct rectangle](../validation/2026-10-15/native-direct-rectangle-v1/README.md) and
[larger source/batch tests](../validation/2026-10-15/native-wide-rectangle-v1/README.md)
qualify custom rendering components. The
[direct comparison and vector checks](../validation/2026-10-15/native-rectangle-comparison-v1/README.md)
retain the exact-comparison failures and corrected vector component pass.
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
