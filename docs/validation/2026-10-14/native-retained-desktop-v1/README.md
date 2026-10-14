# Retained rendering models and a continuous physical bridge

The six-minute native desktop trial completes 724 NPU operations and delivery
acknowledgments without a provider error. It produces 17 indirect-display frames;
the physical presenter submits 15 matching frames during its shorter window,
including both controlled red and green window states. Topology and driver cleanup
pass. Full desktop offload remains unfinished: the bridge is slow, input is not
forwarded, application coverage is incomplete, and Tahoma has not been retested.

## Model lifetime isolation

Source v21 adds an explicit `retained` pipeline-cache policy to v20: 64 uniform,
128 triangle and 16 clear entries, compared with 16, 48 and 4 in `expanded`.
It logs cache counts before requests. Compiled-model memory caching remains off;
the shared OpenVINO core and queue remain enabled. Rendering arithmetic is
unchanged. The policy is experimental and does not change runtime defaults.

Both streams from the [previous investigation](../native-cache-isolation-v1/README.md)
now complete on the NPU. The 117-request stream passes twice, matching all 116
recorded successful outputs; the 256-request stream passes once, matching all
255 reference outputs. Each formerly failing final output matches its isolated
replay. These are consistency checks, not independent CPU oracles for the streams.

The passing traced replay reaches 253 graph and 230 fence create-minus-destroy
calls, with one context and one queue. The earlier failures peaked at 239 and 162
graphs. This supports the model-retirement hypothesis and does not support those
lower counts as a fixed capacity boundary. The trace has no warning/error events;
event loss and physical resource counts have not been independently verified.
The internal Intel failure mechanism is not established. Finite larger caches
will eventually retire entries and do not prove indefinite stability.

## Native trials

| Trial | Native NPU operations acknowledged | Indirect frames | Physical submissions | Outcome |
| --- | ---: | ---: | ---: | --- |
| Live v11 | 0 | 0 | 0 | Preflight rejects another presentation process; installation never begins. |
| Live v11b | 581 | 12 | 0 | Native six-minute run and cleanup pass; relay rejects its requested duration. |
| Live v12 | 724 | 17 | 15 | Native run, relay, controlled colors, presenter and cleanup pass. |

The preflight now pins process handles before evaluating active-process conflicts,
so a stale process-enumeration row cannot reject a trial after that process exits.
The actual cause of the first preflight conflict was not established. The relay's
finite duration limit is corrected from 170 to 300 seconds. A supplemental attempt
finds insufficient remaining time and exits before launching any presentation.
All failed reports remain in the archive.

Live v12 keeps the physical 2880 × 1800 path and a separate 1024 × 768 indirect
desktop. Actual DWM NPU execution, selected renderer, producer identity, source
sequence/timestamp and exact backbuffer bytes are checked. The presenter submits
15 frames with 14 distinct hashes and no shader dispatches of its own. It still
uses the iGPU for uploads, copies and presentation; this is not zero GPU activity.

Source intervals range from 1.143 to 53.445 seconds. The capture-enabled trial is
not a valid comparative performance benchmark, but these long gaps plainly fail
the responsiveness target. The renderer's logs identify expensive graph creation
and thousands of small inference dispatches in some draws. Those are the next
optimization targets. The two successful six-minute trials do not meet the
30-minute stability target.

## Presentation evidence limits

`Present` acceptance and byte-exact backbuffers establish the tested delivery
path, not independently observed scanout or GPU relief. A 90-second PresentMon
capture targeting the live presenter reports 118,842 lost ETW events and no CSV
rows. It fails to qualify display mode. The normalized capture summary and the
original report's hash are retained; redundant PowerShell provider metadata from
an extended-string serialization error is omitted from Git.

Direct swapchain statistics are the next diagnostic route. Microsoft distinguishes
present calls from frames displayed and provides a separate media composition
mode. Neither alone measures residual GPU work.
[Microsoft frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics),
[Microsoft media statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/ns-dxgi1_3-dxgi_frame_statistics_media).

## Reproduction and integrity

`native-routing-v21.patch` applies to v20 from the previous investigation. Sources
and scripts are under `raw`, alongside build identities, replay results, driver
trace summaries, both successful native runs and the failed attempts. The existing
v15 signed driver package is unchanged; only the worker changes. Its registered
D3D11 qualification passes eight NPU operations before native admission.

All 192 collected text artifacts, totaling 3,435,914 bytes, match SHA-256 values
computed on Delta. Of these, 191 are archived; the omitted redundant report is
identified in [raw-manifest.json](raw-manifest.json). Pixel recordings, compiled
models, raw ETL, signing keys and private addresses remain excluded. See
[report.json](report.json) for assertions and measured intervals.
