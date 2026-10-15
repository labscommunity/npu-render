# Full desktop offload: acceptance and execution plan

Updated 2026-10-15. Target: Delta, Windows 11, Panther Lake. The repository stays
private. Full desktop offload is **unfinished**. Six-minute native NPU desktop
trials deliver changing frames to the physical presenter, and independent screen
reads confirm controlled colors with the physical desktop primary. NPU-primary
routing and exact ordinary recovery now work. The controlled input window appears
in completed NPU primary frames. A longer trial now receives mouse input and
shows the resulting green window on the physical output. Corrected live v21 now
passes the complete controlled mouse/keyboard/color sequence and exact ordinary
recovery. V23 also passes finite pointer confinement and foreground reacquisition.
A matched worker comparison cuts thirteen expensive draw operations from about
121 seconds to 3.1 seconds with exact pixels. A responsive usable switch,
application coverage and a Tahoma benefit remain unqualified. The optimized v24
live trial subsequently fails: one new model exceeds the client deadline, and a
later request loses the NPU device. Ordinary physical desktop recovery passes. The isolated provider subsequently
recovers a real device loss during a complete recorded stream. Native v27 passes
1,107 acknowledged NPU operations, 35 matching physical submissions, controlled
input and exact ordinary recovery; response remains too slow.

The end state is one switch that moves desktop graphics work to the NPU, retains
physical display scanout, supports normal interaction, and leaves the iGPU
available for Tahoma. Composition, application rendering, video processing, and
display scanout must be measured separately. An indirect display's preferred
renderer does not establish where every application renders.

## Ordered gates

| Gate | Work | Acceptance evidence | Current status |
| --- | --- | --- | --- |
| 1. Routing | Record active physical and indirect paths before, during, and after admission; identify clone/extended configuration and actual rendering adapter. | Timestamped topology, native DWM NPU receipts, selected adapter, frame producer, exact restoration. | Automatic clone identified; v11 applies extension with the physical path preserved, produces a frame while extended, restores the clone exactly, and restores the final physical baseline. |
| 2. Continuous rendering | Keep the native renderer alive across changing windows and content; diagnose every failed draw; distinguish model preparation from rendering and transport. | Multiple changing frames, timestamps and pixel checks, sustained operation, no hidden fallback. | Finite v23 passes with 1,559 acknowledged NPU operations. Optimized v24 later loses the NPU after 1,096 completed operations and 1,095 acknowledgments. A 93-second cold compilation precedes a client departure. Isolated-provider v27 subsequently passes 1,107 acknowledged operations; recorded recovery from a real loss takes 62.9 seconds. Thirty-minute stability remains unqualified. |
| 3. Physical presentation | Present new NPU frames on the physical output with distinct logical desktops and no recursive mirroring. | Live sequence-to-present correlation, output identity, measured presentation mode and residual GPU work. | Live v13 submits 13 matching frames. Direct statistics reach 13 displayed frames and report both composed and overlay modes; a separate screen reader verifies both controlled colors beneath the owned presenter. GPU residual cost remains unmeasured. Updates remain slow. |
| 4. Interaction and recovery | Migrate the logical desktop, route pointer/keyboard/focus correctly, handle cursor/DPI/resolution, restore exact topology and windows. | Scripted interaction visible on physical output; emergency exit and owned-process failure recovery; ordinary login/lock path retained. | V23 passes relative motion, blue → mouse → green → keyboard → red, two foreground confinement reacquisitions, and exact ordinary recovery. A static standard arrow matches the NPU frame exactly. Secure desktop, sleep/wake, full cursor fidelity and all fault cases remain open. |
| 5. Responsiveness | Optimize resource reuse, graph compilation, dispatch granularity, transfer volume and frame pacing. | Initial target: ordinary desktop interaction at 30 fps, p95 visible response ≤100 ms, 30-minute stable run. Report cold-start time separately. | Not met. Matched v27/v28 replay reduces thirteen expensive operations' mean compile-plus-draw total from 120,944 to 3,100 ms with exact pixels. Complete desktop performance remains unqualified. |
| 6. Application coverage | Admit and implement required graphics operations across browser, video and representative 3D workloads. Establish per-process adapter choice. | Capability matrix plus real hardware runs; every remaining iGPU operation accounted for. | General application/video/game migration unqualified. |
| 7. Tahoma benefit | Run repeated baseline/offload trials under matched power, resolution and application load. | Report Tahoma throughput/latency, desktop frame times, per-engine GPU/NPU/CPU activity, memory and power; demonstrate GPU relief and no material Tahoma regression. | Earlier experiments regressed; native desktop A/B not yet run. |
| 8. Usable switch | Integrate qualified components, install/on/off/status, recovery, documentation and CI; resolve distribution signing requirements. | Repeated clean install and enable/disable cycles, sleep/wake and failures, end-to-end test on target hardware. | Research candidates isolated; no usable full-offload product. |

These are engineering acceptance targets, not measured results or an estimated
completion date. Gate 6 remains open if any requested application graphics class
still runs on the iGPU. Physical display scanout is an explicit remaining hardware
role, and any GPU rendering/copy cost in the bridge must be measured.

## Immediate experiment

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
The complete-stream and native checks remain required before promotion.
Reported binding bytes are not measured host-copy or device-bus traffic.

Keeping uniform shader expressions scalar passes 9.44 million checked components
in each CPU/NPU baseline/candidate configuration, but the recorded candidate
differs at ticket 140. It remains excluded from desktop admission. A diagnostic
separates each graph transformation and compares both providers on that captured
draw before attributing the mismatch to semantics or compiler lowering.

The diagnostic relay and presenter also impose fixed 100 ms waits. Those waits
prevent a 30 fps bridge even if rendering becomes faster. An event-driven relay
and a presenter without the fixed post-present wait are prepared, but their live
behavior remains unqualified. Directory notifications are wake-up hints; frame
identity, completeness and monotonicity checks remain required.
[Microsoft: directory notifications](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findfirstchangenotificationw),
[rearming notification waits](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-findnextchangenotification).

The tested native lease implementation caps desktop admission at fifteen minutes.
A bounded forty-minute ceiling, incremental evidence collection, matching recovery
deadlines, and short/long controllers are prepared for qualification. The long
trial requires advancing presentation timestamps and an independent physical-output
color observation after thirty minutes, followed by exact ordinary restoration.
Prepared code and parser checks do not satisfy the duration gate.

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
