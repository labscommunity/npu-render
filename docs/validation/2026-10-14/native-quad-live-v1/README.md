# Native quad integration: live failure and next constraints

Recorded October 14, 2026 on Delta, Windows 11/Panther Lake, NPU driver 5540 and
OpenVINO 2026.4.1. **Full desktop offload remains unfinished.** The optimized worker
passes the previous failed stream and registered Direct3D qualification, but its
next native desktop trial loses the NPU device. The ordinary physical desktop is
restored. No native-path Tahoma comparison has been performed.

## Results

| Check | Result | Scope |
| --- | --- | --- |
| Previous rollback stream, replay v29 | Pass: 972 requests; 971 original output hashes match and one previously failed request now completes. | Recorded commands, no desktop transition. |
| Registered D3D11 v21 | Pass: eight NPU operations, package removal and cleanup; memory integrity remains enabled. | Native graphics boundary, finite qualification. |
| Native desktop v24 | Fail after 1,096 completed NPU operations and 1,095 delivery acknowledgments. | Fourteen native frames and fourteen exact source/backbuffer submissions; independent physical blue/green/red observations pass. |
| Pointer and ordinary recovery | Relative motion/click/key received; three foreground confinement reacquisitions; windows, cursor, foreground and final physical baseline restored. | Intermediate clone restoration fails after adapter removal during failure cleanup. |
| Original final request, fresh replay v30 | Pass for ticket 1096. | No original successful pixel hash exists; this does not resolve the sequence-dependent device loss. |
| Windows event query | Both bounded System/Application queries complete; no selected warning/error or Intel/NPU/display/WHEA event. | Absence of a selected event does not establish absence of a driver fault. |

Before the final device loss, ticket 1053 takes **93,043 ms** for compilation and
rendering, exceeding the desktop client's 90-second deadline. Rendering itself is
171 ms; backend compilation accounts for 90,323 ms. Five of six compile requests
are cache hits. The provider finishes, but the client has departed and delivery is
not acknowledged. The next DWM stream starts at sequence 1. Ticket 1096 later
fails in `zeFenceHostSynchronize` with `ZE_RESULT_ERROR_DEVICE_LOST`.

This establishes two actionable problems: expensive new models must be prepared
without blocking the active desktop, and sustained NPU execution must survive the
recorded command sequence. It does **not** prove that the timeout causes the later
device loss. Separate traced replay and inference-stage diagnostics follow this
archive.

Private request recording and physical-output diagnostics were enabled. A CPU
build of another candidate overlapped this live diagnostic. Its elapsed times
are not a matched desktop performance result.

## Isolated performance candidates

Worker v29 increases the row-band capacity of the exact integer crop/blend graph.
Both 262,143- and 1,048,575-component capacities pass on CPU and NPU: each run
checks 36 cases and 286,780,416 components, including 2880×1800 output, both channel
orders, clipping and alpha boundaries. Both capacities also match thirteen
recorded draw outputs on repeated execution.

For those thirteen draws, isolated NPU warm time averages 186 ms at 262,143 and
179 ms at 1,048,575. These are unmatched component measurements; neither candidate
has been admitted to a native desktop trial. Worker v30's generic batch experiment
is **unqualified**: its build reaches a packet contract that still rejects the
newly accepted capacity. The failure is preserved and no inference ran with it.

## Evidence and next decision

[report.json](report.json) contains asserted results and the preceding timeout.
[raw-manifest.json](raw-manifest.json) records 248 remotely hash-verified files
(5,639,430 bytes). [source-manifest.json](source-manifest.json), the source scripts
and patches preserve both experimental candidates. Captured pixels, request
payloads, models, compiled caches, binaries and raw ETW traces stay out of Git.

The next gate is reliable rendering with model preparation separated from the
desktop deadline. Then qualify responsive continuous operation, measure remaining
GPU work, cover application/browser/video workloads, and run matched Tahoma
comparisons before calling the switch usable.
