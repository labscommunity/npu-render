# Frame command batching: implementation contract

Status, 2026-10-20: design derived from the measured desktop bottleneck; not an
implemented or qualified rendering path. An initial provider-independent
[command ledger](../../native/include/command_batch.hpp) now implements metadata
admission, ordered version dependencies, immutable command-state capture, bounded
retention accounting, retirement and atomic completion. It has CPU contract tests;
it does not retain actual texture storage or submit drawing work.

The current Windows driver completes every draw synchronously. Its `flush` and
`hazard` callbacks are empty because that synchronous behavior supplies ordering.
`IndexedNpuExchange` sends the destination background with each request and
returns the full destination. `map`, staging copies, presentation and resource
identity rotation currently rely on those completed writes.

The [client timing diagnostic](../validation/2026-10-20/native-client-timing-v1/README.md)
places about 95.6% of measured synchronous request time inside the parent worker.
During its animation, child rendering occupies 6.88 of ten seconds. Therefore
batching only the client transport cannot close the 0.7-to-30-updates/s gap.
The architecture must also reduce NPU submissions and repeated work across draws.
The [clipping result](../validation/2026-10-20/native-resident-clip-worker-v1/README.md)
shows that fusion can accelerate its selected work, while its small overall gain
shows the limitation of optimizing one shader family.

Windows has explicit submission boundaries including presentation, flushing and
resource access. A batch cannot simply defer everything until the next frame.
This is the basis for the proposed boundaries below, not proof that our driver
already implements them.
[Microsoft command-buffer documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/submitting-a-command-buffer).

OpenVINO's NPU request implementation uses command lists, queues and
synchronization objects. Its memory interfaces can share buffers between requests,
subject to lifetime and synchronization requirements. That offers a possible
implementation mechanism; it does not establish that a Direct3D resource can be
imported without copying or that concurrent requests improve this workload.
[OpenVINO NPU request design](https://github.com/openvinotoolkit/openvino/blob/master/src/plugins/intel_npu/docs/inference-request.md).

## Resource and ordering model

Identify each resource by client context, allocation generation, resource ID,
subresource and content version. IDs never become raw device addresses. A queued
draw owns immutable references to its geometry, constants, shaders, samplers and
input versions. Rebinding or destroying an application object cannot change an
already queued draw.

Each write names the expected preceding destination version and a new version.
Read dependencies refer to the precise preceding write, even when several
commands use the same surface. Retained resources eliminate repeated transport
only after their contents and version are known at the receiver. A descriptor
cache or a repeated hash alone does not establish resource ownership.

| Trigger | Required behavior before returning or handing off |
| --- | --- |
| Draw or clear | Validate captured state; enqueue an ordered NPU operation; retain referenced storage. |
| CPU read or read/write map | Complete all relevant earlier writes and make the requested version visible. |
| Nonblocking map | Report still-drawing when needed; never expose stale bytes or poison an otherwise valid context. |
| Discard/no-overwrite upload | Preserve storage referenced by pending commands; publish the new upload version explicitly. |
| Resource copy | Order source reads and destination writes; account separately for host staging and NPU work. |
| Flush | Submit preceding work so completion does not depend on a future application-thread call. |
| Present | Complete the selected surface version before the existing presentation handoff. |
| Identity rotation | Preserve the allocation/version relation across swapchain identity changes. |
| Destroy or lease revocation | Retire pending references and work before releasing their owned storage. |

The flush independence condition is explicit in the Windows interface contract.
[Microsoft Flush documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d11_1ddi_flush).

## Recovery and bounded execution

A batch carries an admission generation, monotonic sequence, bounded command
count, bounded byte budget and complete resource dependencies. Reject malformed,
stale, cross-context or unavailable versions before changing committed state.
Publishing a partially rendered batch as complete is prohibited.

Provider restart must reconstruct the last committed resource versions before
retrying pending work. The existing per-draw recovery receives a complete
background and cannot simply be reused once those bytes are omitted. Either
retain validated checkpoints or retain a bounded replay journal sufficient to
recreate them on NPU. Account for checkpoint traffic and replay latency.

This remains separate from driver/process teardown and physical desktop
restoration. Neither silently switching to CPU rendering nor leaving the user
with a stale frame satisfies recovery.

## Implementation sequence and evidence

1. Inventory captured draw dependencies, resource footprints and readback
   boundaries. Preserve the distinction between resource identity and identical
   pixel contents. The existing capture lacks enough identity information to
   infer a general resource graph from hashes alone.
2. Implement and test bounded resource-version and command-order contracts.
   Cover overwrite/discard, overlapping reads and writes, nonblocking maps,
   identity rotation, destruction and partial failure. These are CPU contract
   tests, not proof of NPU execution.
3. Execute command batches in an isolated NPU worker using existing qualified
   rendering routes. Verify full images against the corresponding ordered
   unbatched operations. Measure protocol bytes, launches, worker time and
   checkpoint/recovery cost separately. A transport-only gain is insufficient.
4. Fuse eligible ordered work within NPU tile batches, preserving overlap,
   coverage, sampling and blending. Prove resource-footprint bounds and exact
   output before interpreting a lower submission count as useful acceleration.
   Keep unsupported operations explicit rather than silently rendering elsewhere.
5. Integrate the passing contracts into the Windows draw/map/copy/flush/present
   callbacks. Repeat registered graphics, ordinary application admission,
   physical frame delivery, input distribution, 30 Hz animation and restoration.
6. Require current-renderer responsiveness and sustained operation before
   claiming a usable switch. Measure residual iGPU work and matched Tahoma
   throughput separately, under matched power, geometry and workload conditions.

The first implementation should preserve a reversible selection between the
existing synchronous route and the new route at context creation. Changing that
selection with outstanding work requires an explicit drain and resource-state
handoff; an environment variable alone is not the user-facing switch.

## Implemented contract boundary

The ledger permits one outstanding batch in a single owned context. Imported
resource generations cannot be reused. Each write must extend the current
destination version; reads may name an older retained version or an earlier
command's output. Future reads, stale writes, cross-context references, sequence
replays and byte-budget overflow are rejected before admission. Completion must
name every ordered output from the current attempt before any committed metadata
changes. Retry invalidates the old attempt; abandonment preserves checkpoints.

The CPU tests include a 10,000-batch independent version model, partial/stale
completion, failure/retry, old/new texture reads, state mutation after submission,
retirement, generation reuse, and budget/overflow rejection. These validate only
the metadata contract. The renderer must still pin actual resource storage,
validate graphics state and outputs, restore checkpoints after NPU restart, and
implement map/write/discard/flush/presentation synchronization. No Windows callback
uses the ledger yet. Identity tombstones consume its bounded context capacity;
production resource recycling needs a drained-context handoff.

## Isolated NPU execution evidence

The [ordered rectangle component](../validation/2026-10-20/native-command-batch-component-v1/README.md)
now passes 120 exact NPU fixtures. It retains per-pixel destination state across
up to 32 commands, with ordered coverage, copy/source-over blending and masks.
Matched synthetic 1024×768 runs take about 46 ms for a batch, versus 242 ms for
separate submissions of the same optimized kernel. This remains above a 30 Hz
frame budget and excludes textures, geometry, driver transport and presentation.
The component does not yet use the ledger or retain general graphics resources.
The next integration must bind actual owned checkpoints to version references
and recover a partially executed batch without publishing partial pixels.

The [owned-checkpoint transaction](../validation/2026-10-20/native-command-batch-transaction-v1/README.md)
now connects the ledger to that NPU kernel in an isolated C++ probe. Consecutive
commits, immutable snapshots, blocked pending reads, recovery after partial NPU
output, stale-completion rejection and abandonment all pass. The next boundary
is Windows resource access: capture explicit object/backing identities and
logical write versions around draw, map/unmap, copy, flush, present and rotation.
Treat shared external aliases conservatively and distinguish application access
from internal staging reads. The trace must not be presented as a benchmark.
