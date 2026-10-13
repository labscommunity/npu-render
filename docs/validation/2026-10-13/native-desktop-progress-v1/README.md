# Native desktop bootstrap, registered graphics, and timed recovery

**The physical Windows desktop has not been offloaded.** This record qualifies
several prerequisites and preserves the assembled native admission controller.
Its desktop installation branch has not run: Delta has no signed-in, unlocked
console user. No Tahoma performance test was performed.

The current production runtime remains the earlier native same-session adapter.
These protocol-3 drivers and orchestration scripts are isolated experiments,
not a supported desktop switch or a claim that arbitrary GPU functions work.

## What ran on Delta

| Check | Result | Limit |
| --- | --- | --- |
| Updated signed native renderer, version 0.0.1303.0 | Six clears and two indexed draws passed through normal D3D11 with real Windows allocations; eight NPU receipts and eight delivery acknowledgments | No desktop process, swapchain presentation, or physical frame |
| Two simultaneous owned consumers, broker v1 and v2 | Each trial passed 20 NPU operations and 20 acknowledgments; six invalid admission cases rejected | Allocation callbacks emulated; neither consumer was DWM or WUDFHost |
| Temporary process permissions, broker v2 | Four grants to the owned controller/worker were restored with exact DACL byte comparisons | Actual compositor/indirect-host requests were not exercised |
| Native process observer | Located and pinned the actual compositor and existing display hosts by image, SID, session, PID, and creation time; rejected unrelated device groups | Read-only discovery; no host admission |
| Automatic recovery, corrected v1b fixture | The scheduled timer fired, removed both staged packages and temporary trust, stopped both owned idle processes, archived two owned diagnostic directories, and unregistered its task | No diagnostic device was installed in this fixture; physical topology restoration was not exercised |
| Assembled launcher's desktop check | Rejected admission while no unlocked owner desktop was available, before installing devices or arming a recovery task | The installation/bootstrap branch remains untested |

Memory integrity remained enabled during registered rendering and recovery tests.
The original Intel display adapter remained healthy. No boot setting changed.
The registered render test installed and removed its actual render-only device;
the recovery timer fixture staged packages without installing devices.

## Prepared desktop test

`run-native-desktop-v1.ps1` checks source/package hashes, prior qualifications,
the controller's identity, console readiness, existing display health, and the
already configured test-signing mode. It arms timed/startup recovery before
staging either package. It preserves physical outputs and does not request a
primary-display change.

`native_desktop_bootstrap_v1.py.txt` starts the indirect-display installation,
finds only a newly created System32 WUDFHost with the exact `T8NativeIddV1` device
group, and pins that process and the console compositor. It starts the NPU worker,
grants bounded query/synchronization rights on its own processes, and publishes
the compositor's lease before releasing the indirect host's admission event.

The trial requires native DriverEntry, selection of the paired render adapter,
an indirect frame packet from the pinned host, successful frame readback, and
NPU execution plus a delivery acknowledgment attributed to the actual compositor.
These conditions establish an indirect desktop-frame path if they pass. They do
not establish that the frame appeared on Delta's physical screen, that current
application windows migrated, that all graphics functions work, or that Tahoma
became faster. The trial removes the indirect device before closing its worker
and leases. The outer recovery also removes the paired render device.

The native DriverEntry has a ten-second admission deadline. The prepared worker
runs for at most 180 seconds; desktop leases expire within 240 seconds. These are
bounded diagnostics, not a renewable production session.

A separate scheduled watcher started at **11:51:21 UTC on October 13, 2026** and
expires at **23:51:21 UTC** (6:51 p.m. Chicago). The archived snapshot records it
waiting, with neither probe nor admission launched. It runs as the authorized
user with S4U logon and checks the same user's unlocked console. Once ready and
other experiments are idle, it attempts the existing finite presentation probe,
then one bounded native admission trial. It unregisters itself on completion,
error, or expiry. Consult its current remote report for live status; the archived
snapshot is not evidence that it is still running later.

The finite presentation probe uses a previously validated static NPU frame and
PresentMon. It does not perform live desktop offload. The earlier one-hour
presentation watcher expired without launching a window.

## Preserved failures and evidence

The broker-v2 launch initially received an array-shaped source manifest while its
validator expected a filename-to-hash object. Validation failed before Python
execution. The corrected manifest was verified before and after the passing run.

The first recovery fixture captured a transiently unavailable `Process.Path` for
its newly started worker. The recovery timer still removed packages and trust,
but rejected that incomplete process identity. The harness stopped the owned
children; an independent cleanup audit verified no owned processes, packages,
trust, or diagnostic directories remained and removed the failed fixture's task.
The corrected v1b fixture waits for a stable path, records it once, and passed the
actual scheduled recovery. Both outcomes are retained.

`raw-manifest.json` covers 114 artifacts whose sizes and SHA-256 hashes were read
from Delta and verified after transfer. Gzip timestamps are deterministic.
Fifteen collected source files also matched their local originals byte for byte.
Python reference sources have a `.py.txt` suffix here; their byte-exact originals
are retained in the raw archive. No model weights, compiled caches, private keys,
or binaries are included.

The worker and signed renderer derive from
[the native trace candidate](../native-trace-progress-v1/). The native indirect
host derives from [the routing candidate](../native-routing-progress-v1/).
The remaining physical-screen and topology work is tracked in
[the physical presentation plan](../../../research/physical-desktop-presentation.md).
