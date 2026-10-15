# Isolated NPU execution and bounded recovery

**Full desktop offload is unfinished.** A new candidate keeps the authenticated
desktop connection alive while a separate process executes NPU rendering. Exact
output, forced worker termination, simulated device loss and owner-exit cleanup
pass on Delta. Recovery from an actual device loss, full-stream reliability of
this candidate, registered-driver integration and a native desktop trial remain
pending in this archive. No Tahoma comparison has been performed.

## Reliability evidence

The original optimized worker fails again without changing display topology:
recorded replay v31 completes 820 requests and then reports
`ZE_RESULT_ERROR_DEVICE_LOST`. Its failed request is a different shader family
from the last failed request in native v24. The failure is not specific to a live
desktop transition or one captured input.

A subsequent diagnostic worker, with the same rendering policy and an opt-in
inference history, completes three passes of the 1,097-request stream. All **3,291
requests** receive delivery acknowledgments: 3,288 outputs match their original
reference hashes, and the original final failed request completes on all three
passes without an original successful reference. The run lasts about 16 minutes
49 seconds. It does not establish that the intermittent device loss is fixed.

The failed replay's ETW selection records one context, one queue, a peak of 819
graphs and 754 fences, and balanced final creation/destruction counts. A fence
wait reports device loss; three subsequent driver assertions mention queue idle
state during cleanup. **The trace lost 628 events**, so these lifetime counts are
not exhaustive and do not exclude a resource leak. Raw traces remain private on
Delta. The successful soak's circular trace may omit earlier events and is not
used to establish resource lifetimes.

## Recovery candidate

Workers v33/v34 keep the existing authenticated client connection in a supervisor
and forward complete rendering packets to an owned NPU child. Each packet already
contains the source pixels, background and state needed to compute its result.
The supervisor validates the response before acknowledging the original request.

The candidate permits one retry per request and three restarts per finite worker
session. An explicit device-loss error permits a fresh child and NPU retry; an
invalid response does not qualify for recovery. The child uses the same executable,
a new private endpoint, pinned process identity and a job that kills it if its
owner exits. No CPU/GPU fallback is added. V34 reports the error before potentially
slow provider destruction, so the supervisor can recognize it promptly.

| Hardware check | Result |
| --- | --- |
| Direct diagnostic worker, five recorded requests | Five exact outputs and acknowledgments. |
| Isolated NPU child, five requests | Five exact outputs and acknowledgments; one generation. |
| Kill the child before the fourth request | Replacement NPU child completes the request; all five outputs match and are acknowledged once. |
| Throw a marked synthetic device-loss error in the child | Same bounded recovery and five exact outputs. The injected error is explicitly not a real device loss. |
| Kill the supervisor after one verified NPU result | The pinned child exits in approximately 29 ms in both qualification versions. |

These are component tests with controlled faults. They do not demonstrate
successful desktop recovery after an actual NPU reset. The supervisor's reported
draw time includes child transport, compilation and any recovery; it is explicitly
not comparable with earlier in-process inference-only timings.

## Preparation evidence

Compilation tracing records graph identity, operation/input counts, elapsed time
and cache status without pixels or model contents. The five-request diagnostic
loads 25 graphs, all from cache. Original ticket 1053, which previously exceeded
the client deadline, now finishes compilation and rendering in **1,289.5 ms**;
rendering is 151.9 ms. All six of its compile requests hit the cache. This is an
unmatched preparation diagnostic, not a desktop performance comparison.

The new trace identifies a 17,057-operation graph among those six. The earlier
cold run did not record per-graph identity, so attribution of its entire stall to
that graph remains unproven. General preparation across new application shaders
and resource shapes is still required.

## Provenance and next gate

[report.json](report.json) asserts the results. [raw-manifest.json](raw-manifest.json)
covers 138 remotely hash-verified files (5,827,223 bytes).
[source-manifest.json](source-manifest.json) and four ordered patches preserve
workers v31–v34; v31 starts from the previously qualified worker v28. Sources,
build receipts and failure diagnostics are included. Pixels, captured request
payloads, compiled models/caches, binaries and raw traces are excluded.

The full recorded-stream soak through the isolated provider is separate and was
running when this archive was assembled. Require it to pass before registered
and native desktop qualification. Then address responsive rendering, application
and video coverage, residual GPU work and matched Tahoma performance. CPU builds
and diagnostics overlapped parts of the recorded correctness runs; none of these
results is a matched performance qualification.
