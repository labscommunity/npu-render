# Device-loss packet replay and generation-aware evidence

The request captured during the actual
[live61 device-loss failure](../native-resource-boundaries-v1/README.md) completes
three times on worker 24 and three times on worker 19. All six NPU outputs have
SHA-256 `68a32b846344b3eb86702e3b4846e6a3468ef8905fb1d4a630c34ca449da970c`.
Neither isolated phase reports a provider failure or restart.

| Worker | Individual request elapsed times |
| --- | --- |
| 24 | 713.72, 628.19, 622.93 ms |
| 19 | 722.85, 561.82, 575.79 ms |

This is an isolated packet replay, including first-use preparation and subsequent
executions. It is not a matched performance comparison or an independent pixel
oracle, and does not reproduce the preceding desktop draw sequence. The original
device-loss cause remains unresolved. The request's integrity pin and synthesized
capture index are retained; its private pixel payload is not committed.

The shared-application evidence join now selects the child generation from each
parent response. It validates child process identity, preserved application
sequence, complete shader/operation identity, successful NPU receipts,
acknowledgments and a unique nested QPC interval. Child ticket numbering is
allowed to restart; it is not assumed equal to the parent ticket.

This audit passes eight application operations from the one-generation live60
capture and eight from generation 2 of the failed live61 capture. Thirteen
corrupted variants are rejected, including wrong generation/process/shader,
dimensions, sequence, clock, timing interval, unproved retry, duplicate parent
response, GPU response, and missing child completion or acknowledgment.
These are CPU evidence tests using hardware receipts, not new rendering tests.
The failed live61 trial remains failed.

A separate provider audit retains one initialization per exercised route in each
child, checks each balanced geometry schedule, and accounts for explicit NPU
recovery responses. It passes both captures and rejects duplicate or missing
imports, an invalid schedule, a GPU response and an unknown generation. Aggregate
import counts may increase after recovery only when those per-child checks pass.
This supplements the existing frame, application, source-identity and restoration
requirements.

The manifest preserves 27 artifacts: both replay phases and their pinned
sources, correlation qualification, provider audits and negative checks. No full
desktop offload, GPU relief or Tahoma improvement is established here.
