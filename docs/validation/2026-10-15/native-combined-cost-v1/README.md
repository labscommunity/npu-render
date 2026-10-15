# Matched batching and compiled-model sharing costs

The combined candidate reproduces the finalized native desktop recording exactly,
but its measured benefit is small and depends on the workload. On the complete
recording, larger generic batches alone are **8.54% slower**. Adding compiled-model
sharing brings the combination to **3.06% faster** than the original configuration.
On the separate 65-draw sample, the same combination is **4.25% slower**.
It does not meet the proposed 10% improvement gate and is not promoted here.
Full desktop offload and a Tahoma benefit remain unfinished.

## Complete recording

All conditions use the same worker v37 binary, shared provider core and queue,
retained pipelines, qualified quad path, and isolated NPU process. The order is
A1, B1, C1, C2, B2, A2. Each phase replays the same 1,109 finalized requests;
all 1,108 available original pixel hashes match, and all 1,109 deliveries are
acknowledged. The final shutdown request has no original successful reference.
Every phase uses one provider generation with no recovery. All model requests
served by the disk cache are hits. AC power is present before and after each phase.

| Condition | Generic batch | Compiled-model sharing | Mean preparation plus draw time | Change against A |
| --- | ---: | --- | ---: | ---: |
| A | 4,096 | Off | 344.366 s | Baseline |
| B | 16,384 | Off | 373.762 s | 8.54% slower |
| C | 16,384 | On, 512 entries | 333.826 s | 3.06% faster |

Sharing improves C over B by 10.68%, but batching offsets most of that benefit.
Larger batches reduce inference submissions from 39,065 to 33,538 per replay,
while increasing drawing time. Sharing reduces backend model loads from 1,352
to 289, with 1,063 memory-cache hits. These counts explain why fewer submissions
or model loads cannot substitute for measuring total cost.

The comparison covers six complete replays, with 6,648 reference matches and
6,654 acknowledged requests. It measures aggregate worker preparation and draw
time, not desktop frame rate, visible response or Tahoma throughput. The
4,096-sample configuration with sharing enabled is not separately timed here.

## Separate expensive-draw sample

The earlier 65-request sample uses warm-A, warm-B, A1, B1, B2, A2. A uses 4,096
samples with sharing off; B uses 16,384 with sharing on. All 390 outputs, including
warmups, match their references and are acknowledged without provider recovery.
The measured phases use cached models. Mean preparation plus draw time rises
from 95.282 to 99.335 seconds, a 4.25% regression. The different result on the full
recording demonstrates the importance of the actual workload mix.

## Finalized capture and reporting corrections

The live-v27 report records a checkpoint of 1,107 acknowledged operations.
After shutdown, its capture contains 1,109 requests and 1,108 output references.
Request 1,107 has an output reference beyond the live checkpoint; request 1,108
is captured during shutdown without an original successful output reference.
The replay correctly executes all 1,109, matches all available references and
receives all delivery acknowledgments.

The original qualification controller incorrectly requires 1,107 for all three
counts, so it fails despite the completed replay passing. Its dependent timing
controllers abort before running. Corrected qualification v3 checks the finalized
capture index, output hashes, parent acknowledgments, child receipts, build identity
and absence of recovery. It reuses the completed hardware evidence rather than
rerunning a passing replay to fix a controller assertion.

An intermediate v2 report also incorrectly labels the response index's
`delivery_acknowledged` field as an acknowledgment count. That index is written
before delivery and is not an acknowledgment ledger. V3 corrects the label and
uses the parent worker receipts. The original reports, including this incorrect
intermediate label, are preserved. The live checkpoint is not rewritten.

## Reproduction and integrity

The combined source patch applies to worker v34 from the
[provider recovery archive](../native-provider-recovery-v1/README.md). Its enlarged
batch and sharing components were independently qualified in the
[previous archive](../native-batch-sharing-v1/README.md).

All 144 collected text artifacts, totaling 17,804,016 bytes, match hashes computed
on Delta. The archive includes the clean-build receipt, source identities,
controllers, failed qualifications, exact replay records, child timings and
comparison reports. See [report.json](report.json),
[raw-manifest.json](raw-manifest.json) and [source-manifest.json](source-manifest.json).
Desktop pixels, binaries, model weights and compiled caches remain outside Git.
