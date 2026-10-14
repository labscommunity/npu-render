# Full desktop offload: acceptance and execution plan

Updated 2026-10-14. Target: Delta, Windows 11, Panther Lake. The repository stays
private. Full desktop offload is **unfinished**. A six-minute native NPU desktop trial
now delivers 15 changing frames to the physical presenter with exact bytes and
clean restoration; a usable switch and a Tahoma benefit have not. The next trial also passes, and
independent screen reads confirm both controlled colors on the physical desktop.

The end state is one switch that moves desktop graphics work to the NPU, retains
physical display scanout, supports normal interaction, and leaves the iGPU
available for Tahoma. Composition, application rendering, video processing, and
display scanout must be measured separately. An indirect display's preferred
renderer does not establish where every application renders.

## Ordered gates

| Gate | Work | Acceptance evidence | Current status |
| --- | --- | --- | --- |
| 1. Routing | Record active physical and indirect paths before, during, and after admission; identify clone/extended configuration and actual rendering adapter. | Timestamped topology, native DWM NPU receipts, selected adapter, frame producer, exact restoration. | Automatic clone identified; v11 applies extension with the physical path preserved, produces a frame while extended, restores the clone exactly, and restores the final physical baseline. |
| 2. Continuous rendering | Keep the native renderer alive across changing windows and content; diagnose every failed draw; distinguish model preparation from rendering and transport. | Multiple changing frames, timestamps and pixel checks, sustained operation, no hidden fallback. | Retaining rendering models clears both reproduced failures. Native v11b and v12 each complete six minutes, with 581 and 724 acknowledged NPU operations and 12 and 17 frames. No provider errors occur; 30-minute stability and behavior at eventual cache eviction remain unqualified. |
| 3. Physical presentation | Present new NPU frames on the physical output with distinct logical desktops and no recursive mirroring. | Live sequence-to-present correlation, output identity, measured presentation mode and residual GPU work. | Live v13 submits 13 matching frames. Direct statistics reach 13 displayed frames and report both composed and overlay modes; a separate screen reader verifies both controlled colors beneath the owned presenter. GPU residual cost remains unmeasured. Updates remain slow. |
| 4. Interaction and recovery | Migrate the logical desktop, route pointer/keyboard/focus correctly, handle cursor/DPI/resolution, restore exact topology and windows. | Scripted interaction visible on physical output; emergency exit and owned-process failure recovery; ordinary login/lock path retained. | VM interaction proof exists; native interaction unqualified. Next: make the NPU logical desktop primary, keep the physical presenter on the separate output, and qualify ordinary Windows input plus cursor/window restoration. |
| 5. Responsiveness | Optimize resource reuse, graph compilation, dispatch granularity, transfer volume and frame pacing. | Initial target: ordinary desktop interaction at 30 fps, p95 visible response ≤100 ms, 30-minute stable run. Report cold-start time separately. | Not met; several individual draws take seconds. |
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

Next, qualify primary-desktop migration and normal Windows input with exact
cursor/window/topology recovery. Continue targeting the measured expensive draws
with independently checked copy/blend specializations. Then establish responsive
long-duration operation, application coverage and matched Tahoma benefit. Every
gate remains explicit; a component pass does not complete a later gate.

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
