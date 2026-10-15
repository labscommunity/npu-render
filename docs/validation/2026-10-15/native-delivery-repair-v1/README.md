# Delivery repair and long-run failure diagnosis

Full desktop offload remains unfinished. The corrected indirect-display lease
admits a 38-minute native trial, but the run ends early with a controller error and
a later NPU worker timeout. Its 1,085 monitored acknowledged operations do not
qualify thirty-minute stability. The controller's earlier file-not-found exception
has no saved call site, so its cause remains unresolved. Display and pointer cleanup
pass. The next controller saves the exception location immediately and watches
relay/native process health throughout presentation.

An isolated replay of the captured failing request reproduces the timeout without
changing the desktop. A 17,057-node shader graph takes **89,091.697 ms** to compile
from scratch, consuming almost the entire 90-second request deadline. The replay
times out and cleans up its owned worker. The next replay loads that graph in
201.484 ms and completes the request in 1,620.025 ms. It has no successful original
pixel reference; this is evidence of successful execution after caching, not a
new correctness qualification or a general solution for previously unseen shaders.

A separate controlled application successfully uses the ordinary default D3D11
adapter and executes six clears and two indexed draws on the NPU. All eight
operations have matching acknowledgments and hardware child receipts, exact
fixture pixels, and zero CPU/GPU execution counts. The application does not call
the driver's configuration export or explicitly choose an adapter. It still
requires an owned two-minute admission lease; automatic admission and general
browser/video/game coverage are unqualified. The initial probe incorrectly assumed
only one kernel adapter alias; both that failure and its correction are retained.
This component ran during the longer desktop trial and is not a matched performance
measurement.

The frame-publication fixture reproduces Windows error 5 with a reader holding the
target file open. Bounded retries pass release-after-delay cases, including a
shared-delete reader; a persistent conflict times out and preserves the prior
complete frame. An initial assumption that shared-delete permits immediate
replacement fails on Delta and is preserved. Windows documents sharing permissions
as lasting until the handle closes; rename requires delete access.
[Microsoft: CreateFileW](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

The new delivery validator rejects the captured live-v31 false positive and eight
synthetic failure cases. It requires successful relay/presenter completion, exact
source-to-submission identity, advancing frames through the trial and an owned
physical color observation near the end. Its 60-second liveness bound is deliberately
a diagnostic gate; the product target remains 30 fps and p95 response at or below
100 ms. The cooperative-stop topology helper passes its layout contract and both
pointer recovery cases. Native cancellation and live delivery still need a new run.

Separately, an isolated combined shader/blending graph matches 789,504 packed
components on each of CPU and NPU. All six scenarios retain exact intermediate
shader values and check final pixels. The experiment reduces two inference calls
to one, but its fixed test order is not a counterbalanced speed comparison. It is
not integrated into desktop rendering.

The archive includes 169 remotely hashed raw files, full controlled-application
source, diagnostic patches and their source manifests. Raw frame pixels, request
packets and compiled caches remain outside the repository. No Tahoma performance
claim follows from these results.
