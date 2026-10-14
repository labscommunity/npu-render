# Primary desktop routing, missing window content, and exact alpha blending

Full desktop offload remains unfinished. Windows accepts the NPU indirect desktop
as primary while the physical display retains its 2880 × 1800 mode at a separate
logical position. The second six-minute trial completes native rendering and
restores topology, the captured ordinary window, cursor and foreground focus.
Neither primary trial displays the controlled input window through the NPU
capture, so neither sends the scripted click or key. Native input is unqualified.

This archive contains 256 remotely SHA-256-verified text artifacts (3,344,359
bytes), source snapshots and candidate patches. Captured frame/request binaries,
compiled models and device caches remain on Delta. The repository remains private;
no candidate is promoted to runtime defaults.

## Primary desktop trials

| Trial | Native rendering | Physical bridge | Recovery | Input result |
| --- | --- | --- | --- | --- |
| v14 / controller 25 | 476 NPU operation receipts, 475 delivery acknowledgments, five changing frames; subsequent device-loss failure | Five exact source-to-present submissions | Experimental devices removed and physical baseline restored; exact intermediate topology and window recovery fail | Owned window is visible at (64,64); no blue pixels in the inspected captured frame; no input sent |
| v15 / controller 26 | 481 NPU operation receipts and acknowledgments, four changing frames; native trial passes | Four exact source-to-present submissions | Exact intermediate topology, one ordinary window placement, cursor and foreground restored; final driver cleanup passes | Owned window passes hit testing, is uncloaked, and its own GDI DC contains blue; no matching captured surface and no input sent |

The virtual primary is 1024 × 768 at (0,0). The physical output is 2880 × 1800 at
(1024,0). The presenter selects the physical route explicitly instead of assuming
the primary monitor is the physical output. It still uploads/copies frames through
the iGPU; residual GPU work is unmeasured. This is not native-resolution offload.

The input window initially paints blue; its owned mouse handler would paint green,
and its owned keyboard handler would paint red. The controller requires matching
NPU frame submissions **and** independent physical-screen color observations before
issuing each event. These gates never open. A successful window paint or a passed
native-renderer report is not counted as successful input or full desktop offload.

The first trial exposes a 92.6-second draw, including 90.0 seconds of provider
compilation. Its reply arrives after the client departs; later work reports
`ZE_RESULT_ERROR_DEVICE_LOST`. The final failing draw succeeds alone. A fresh full
replay completes all 477 requests, matches all 476 available original output hashes,
and acknowledges every reply. This does not establish the device-loss root cause.
It does establish that the final draw is not inherently unexecutable.

The second trial still encounters new graph compilations lasting 24.1 and 27.4
seconds despite the earlier replay. Warm caches do not cover every new resource
shape. These diagnostic runs record requests and are not clean performance tests.

Exact known-color byte searches find no qualifying blue surface in either primary
request stream. The earlier successful secondary-desktop stream contains red or
green test-color patterns in 184 requests, with counts up to 131,072. Raw pattern
counts are a diagnostic clue, not a rendering oracle. Together with the owned
window DC check, they direct the next investigation toward composition/routing,
including the input window's topmost style. They do not prove an overlay mechanism.

## Recovery changes

Topology helper v6 adds primary routing, ordinary-window/cursor/focus snapshots,
an emergency Ctrl+Alt+Shift+F11 exit, and construction-contract checks. The wrapper
checks the actual child exit code instead of relying only on the scheduled task.

In v14, native recovery removes the indirect device while the topology helper is
restoring the prior clone. `SetDisplayConfig` returns error 87; the physical
baseline is healthy, but the helper skips window restoration. Helper v7 checks for
this single-physical-display baseline, permits window restoration in that case,
detects route changes during the trial, and records placement metadata before
applying changes. No window titles or document content are recorded.

V15 exercises ordinary exact restoration successfully. The newly added early
device-removal branch and forced-process termination recovery still need deliberate
fault-injection tests. Snapshot logging alone is not an independent recovery service.

## Alpha-copy compiler investigation

Candidate 26 extends the previously qualified opaque copy path to exact point
samples with unit color modulation, preserving alpha and optionally applying
premultiplied source-over. CPU/NPU standalone tests and eight integrated sampler
tests pass, but recorded replay fails at request 179 with Intel's compiler error:
`Can't convert a F32/SI32 workload as CostModel does not support`.

The original standalone fixture pads source width by 32 pixels. An equal-width
fixture reproduces the compiler failure on the NPU twice while passing on CPU.
This is a missed graph-shape case, not a successful qualification.

Alpha-copy v3 replaces integer division in source-over with an exact identity.
For byte values `d` and `a`, let `t = d * (255 - a) + 128`. Rounded division of the
product by 255 is exactly `(t + (t >> 8)) >> 8`; add the integral source component
and clamp to 255. The generator exhaustively checks the identity over all 65,536
byte pairs. All pixel arithmetic remains on the selected inference device.

The enlarged fixture covers padded and equal-width sources, channel swaps,
replacement and source-over, clipping, point offsets, random bytes and repeated
frames. CPU, NPU and a repeated NPU run each pass **1,146,912,768 component checks**,
4,608 provider calls and 14 negative cases. Candidate 27 integrates this version
and builds successfully; this archive does not qualify it for registered or native
desktop use. Its integrated/replay qualification proceeds separately.

OpenVINO defines the integer operation used by this graph in its
[BitwiseRightShift documentation](https://docs.openvino.ai/2026/documentation/openvino-ir-format/operation-sets/operation-specs/bitwise/bitwise-right-shift-15.html).
The native input probe uses the ordinary Windows
[SendInput API](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
only after checking its owned target. Foreground restoration is verified because
[Windows restricts SetForegroundWindow](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow).

## Remaining work

Resolve the missing primary-window content, complete visible native interaction
and fault recovery, eliminate the measured compilation/dispatch delays, establish
application coverage, and run matched Tahoma baseline/offload trials. Neither
full desktop offload nor a Tahoma benefit is demonstrated here.
