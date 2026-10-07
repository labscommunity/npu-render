# Normal-user controls and desktop preflight

The previous interactive probe exited before producing a report because its
scratch directory granted write access to administrators and only read/execute
access to ordinary users. The installed application's per-user directory already
had the correct inherited user permissions. Moving test outputs to
`%LOCALAPPDATA%\npu-render\checks\interactive-ee4d711` fixed the probe. No directory
ACL, desktop permission, UAC setting, screen-saver policy or authentication state
was changed.

## Installed prototype under the desktop user

The exact [ee4d711 CI bundle](../bundle-ee4d711/README.md) ran in console session 1
through a limited interactive task. The control validator accepted the sequence
GPU → NPU → CPU → GPU. An invalid selection between NPU and CPU was rejected,
and frames retained the NPU backend until the valid CPU switch.

All 1,145 submissions returned `DXGI_STATUS_OCCLUDED`: 367 GPU, 482 NPU and 296 CPU.
Full-shape qualification differed by at most one byte. The explicit
`--allow-occluded` validator option accepted rendering/control recovery only;
`all_present_calls_s_ok` and `scanout_measured` remain false. This run establishes
normal-user control and reporting, not visible presentation or a Tahoma gain.

## Reject an unavailable desktop before inference

`npu-render session --require-desktop` now records the process session, its
connection state, window station, thread/input desktops and monitor count. The
native contention harness checks these before inference warmup and around each
trial, retaining the failed inspection or pending trial. Its existing driver and
presentation checks remain in place.

On Delta, both the command and harness rejected these actual states with exit 1:

| Process context | Observed desktop | Result |
|---|---|---|
| SSH, session zero | Input desktop unavailable, error 1 | Rejected before warmup; zero trials |
| Limited desktop user, session 1, active connection | Thread `Default`, input `Screen-saver` | Rejected before warmup; zero trials |

The loaded-driver check required and observed 1004724 in both harness runs, so
the failures came from desktop preconditions. The test endpoint was deliberately
unavailable; each retained error came from the desktop check, before any request.
No inference server was started. Active/default, disconnected, screen-saver and
access-denied fixture cases are covered by unit tests. An actual unlocked/default
desktop has not yet been qualified by this new preflight on Delta.

This is a snapshot, not continuous display monitoring. Per-frame presentation
results remain necessary, and physical scanout still needs separate measurement.
The implementation only reads documented Windows APIs and closes owned handles:
[OpenInputDesktop](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-openinputdesktop),
[GetUserObjectInformationW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-getuserobjectinformationw),
and [WTSQuerySessionInformationW](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/nf-wtsapi32-wtsquerysessioninformationw).
Checking connection state matters because a disconnected session may expose the
desktop that would become active when reconnected, as the OpenInputDesktop
documentation explains. No input was sent and no desktop was switched.

## Remaining work

The screen saver still prevents visible native testing. Driver package 5540 is
installed, but the runtime still reports 4724 and full-restart approval remains
pending. The native Tahoma comparison and new-driver sampling/compiler experiments
remain open. Temporary tasks were removed after these checks; the installed
prototype and its saved global backend selection were preserved.

`manifest.json` records hashes of unchanged gzip-compressed raw captures. Adjacent
JSON normalizes encoding and formatting. The `passed` fields in `*-status.json`
mean that the expected rejection was observed, not that the desktop was usable.
