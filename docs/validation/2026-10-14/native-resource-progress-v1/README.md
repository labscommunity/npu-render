# Larger triangle targets and a recovered live trial

Renderer candidate `native-routing-v13` qualifies the previously rejected
24-vertex, full-resolution draw, adds capture of a terminal provider request,
and retires an evicted compiled-cache entry before creating its replacement.
The subsequent `native-live-v3` trial has **no observed NPU device-loss error** and
restores both the original clone and the final physical baseline exactly.
It **fails live delivery acceptance**: the physical presenter receives only one
frame in its 80-second interval. Full desktop offload remains unfinished.

DWM completes 386 NPU operations with 386 transport acknowledgments. Three distinct
1024×768 source frames arrive: the first after 63.5 seconds, the next 94.14 seconds
later, then another 6.19 seconds later. The latter two arrive after the presenter
and controlled stimulus have ended. This does not establish responsive or
sustained controlled-content rendering. Three further resource-table draws are
rejected, causing rendering-state recreation; the captured first rejection is
preserved only as metadata, with its desktop pixels excluded from this archive.

## Qualified changes

- ABI 22 admits bounded large single-source triangle targets while retaining
  shader, geometry, blend, reply and execution-budget checks. The captured
  2880×1800 result has 20,736,000 exact CPU/NPU-matching bytes; the local NPU worker
  and transport deliver the same pixels with one operation and one acknowledgment.
- Cache retirement is tested on CPU and NPU, including live request ownership,
  context lifetime and an injected compile failure. The live result does not
  establish that this ordering change fixes the earlier device-loss cause.
- The candidate passes ten native diagnostic operations, package staging/removal,
  and eight operations through ordinary registered Direct3D. Memory integrity
  remains enabled and owned ACL, device, package and trust cleanup passes.

The aggregate worker draw time is 87,471.129 ms and compile-plus-draw time is
163,081.430 ms across 12,921 provider requests. Setup and backend-compile counters
are nested in that aggregate and must not be added to it. There are 201 compiled
cache evictions. These are trial work totals, not a steady-state frame rate or a
Tahoma benchmark. The three packet rejections and many small dispatches remain
specific compatibility/performance targets.

[report.json](report.json) contains the asserted outcome. The archive preserves
137 remotely hash-verified text files and their original/compressed hashes in
[raw-manifest.json](raw-manifest.json). The renderer patch is relative to
`native-routing-v12b` from the preceding large-surface archive. Source hashes and
controller scripts accompany it. Default runtime code is not promoted. No desktop
pixels, model caches or binaries are committed.
