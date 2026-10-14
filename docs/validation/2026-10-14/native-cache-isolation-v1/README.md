# Native request recording and runtime-state isolation

Full desktop offload is unfinished. This investigation captures reproducible NPU
failures and qualifies a texture-upload optimization. Neither establishes usable
desktop rendering or a Tahoma benefit. Defaults remain unchanged.

## Ordered request recording

Source v20 adds optional, bounded recording of the exact requests entering the
serialized NPU renderer and hashes of its completed pixel outputs. Recordings
stay in a private directory on Delta, require 17 GiB free, and stop at 16 GiB.
Capture includes desktop pixels, so only indexes and text evidence enter Git.
Recording adds disk writes and hashing: these live trials are not valid
performance measurements. Recorded provider responses precede client delivery;
separate acknowledgments establish delivery.

The diagnostic probe records and replays ten requests with exact output hashes.
The signed driver package then passes eight operations through registered D3D11,
with installation and removal verified.

## First captured native failure

Live v8 acknowledges 255 NPU operations and submits one byte-exact frame to the
physical presenter before device loss. The frame contains wallpaper, not the
controlled window color. Driver cleanup passes; topology-helper restoration
fails because the indirect display has already been removed. The live trial fails.

Its recording contains 256 requests totaling 1,412,853,108 bytes. Replaying it
with the same settings matches all 255 successful outputs, then stalls at the
same last request. The replay client reaches its deadline; the worker is stopped
before a provider error is printed. The live run itself reports device loss.

The last request alone succeeds. Disabling the custom compiled-model memory
cache then permits two complete replays: 256 NPU acknowledgments each, all 255
reference output hashes exact, and the final output identical to its isolated
replay. This establishes consistency with previous outputs, not independent CPU
validation of every pixel.

## Live validation disproves a complete cache fix

The longer presenter and topology helper build successfully; eleven frame-reader
acceptance/rejection checks pass. The cache-disabled configuration passes eight
registered D3D11 operations.

Live v9 stops before installation because the nonsecure ASUS screen saver is
active. Its pinned process is dismissed without changing screen-lock settings.
Live v9b then acknowledges 116 NPU operations but produces no frame before device
loss. Driver, trust and process-access cleanup pass. The native capture contains
117 requests totaling 301,991,984 bytes.

The failing request is an untextured 17 × 17 draw with 45 vertices. It succeeds
alone but fails after the preceding requests. Controlled results are:

| Configuration for the second recording | Successful requests | Reference hashes matched | Outcome |
| --- | ---: | ---: | --- |
| Compiled-model cache off; expanded pipeline cache; shared queue | 116 | 116 | Device loss on ticket 116 |
| Compiled-model cache off; default smaller pipeline cache; shared queue | 79 | 79 | Device loss on ticket 79, a cached 11 × 11 clear |
| Compiled-model cache off; expanded pipeline cache; separate queues | 116 | 116 | Device loss on ticket 116 |

Tickets are zero-based. Cache disabling resolves the first recorded sequence but
is insufficient for live reliability. Smaller retention and separate queues
also fail. Model retirement is a hypothesis to test, not an established internal
driver diagnosis.

An initial isolated-replay harness attempt supplied a timeout above the client's
90-second limit and was rejected before rendering. Its failed report is retained;
the corrected attempt succeeds. The corrected replay harness gives the worker up
to 45 seconds after client failure to report its provider error before termination.

## Driver trace evidence

Bounded ETW captures use the installed Intel NPU Level Zero and kernel providers.
Raw ETL files remain private on Delta. Filtered summaries preserve model, queue,
context and fence lifetime calls, failed waits and warning/error events.

The failing expanded-cache replay has a peak of 239 graph create-minus-destroy
calls, one context and one shared queue; the smaller-cache replay peaks at 162
graphs and also fails. These are observed API-call balances, not an independently
measured hardware capacity. At teardown, the Intel driver emits three
`queue.is_idle()` assertions from `zeSharedBufferManager.h`. Those assertions
occur after the failed wait, so they do not prove the initiating cause.

The unfiltered trace parser was stopped after the filtered summaries completed;
its partial output and explicit aborted report are preserved. Trace event loss
has not been independently checked.

The shared-queue setting is a private property restricted by our experiment to
the pinned OpenVINO build. Its upstream default is false.
[OpenVINO pinned options source](https://github.com/openvinotoolkit/openvino/blob/e213a147257/src/plugins/intel_npu/src/al/include/intel_npu/config/options.hpp).

## Scoped texture reuse

Source v19 is a separate candidate based on v18; it does not include the recorder.
Within one triangle draw, immutable decoded textures are uploaded once and reused
across raster batches. Vertex inputs and constants still update on each call;
shader execution stays on the selected provider. A new scope requires fresh
uploads, and recompilation also resets upload state. Nested scopes and ordinary
execution during a scope are rejected.

CPU and NPU scope checks each validate 480 output components across six texture
generations and twelve rejected scope uses. The NPU check reports 3,072 bytes
uploaded and 4,608 bytes of repeated host copies avoided. Ten additional existing
regression checks pass. Avoided host copies do not measure device DMA savings,
desktop speed or Tahoma throughput. This archive contains no native validation of
that optimization.

## Reproduction

The v19 and v20 patches independently apply to v18 from the
[tiled-copy investigation](../native-tiled-copy-v1/README.md). Presenter v9 and
topology helper v4 extend only their finite observation limits; their patches
apply to presenter v8 and topology helper v3. Sources, scripts, binary/source
identities, successes and failures are preserved in the archive.

All 362 collected text artifacts, totaling 2,932,376 bytes, were checked against
SHA-256 values computed on Delta. Repetitive successful per-request client logs
are omitted; replay output hashes, complete worker logs and nonempty client error
logs remain. Desktop pixels, compiled caches, ETL binaries and signing keys are
excluded. See [report.json](report.json) and [raw-manifest.json](raw-manifest.json).
