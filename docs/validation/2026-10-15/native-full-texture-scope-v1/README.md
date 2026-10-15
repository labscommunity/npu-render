# Complete-stream scoped texture and model reuse

The candidate reduces recorded preparation plus drawing time by **14.52%** in
an A/B/B/A comparison on Delta's NPU. All 4,432 available reference pixel hashes
match. Four final requests have no original successful reference; those pass
execution and acknowledgment checks only. This qualifies the complete recorded
workload, not native desktop responsiveness or a Tahoma improvement.

| Run | Scoped uploads / memory model cache | Preparation + drawing |
| --- | --- | ---: |
| A1 | Off / off | 351,681.673 ms |
| B1 | On / on | 290,802.627 ms |
| B2 | On / on | 303,190.060 ms |
| A2 | Off / off | 343,234.522 ms |

Each run replays the same 1,109 requests, uses one hardware provider process,
acknowledges every operation, and executes 39,065 provider requests. No provider
failure or retry occurs. AC power is present before and after each run. All
reported disk cache requests hit; these are not cold-start measurements.

Model cache reuse lowers backend compile calls from 1,352 to 280 per run, with
1,072 memory cache hits and no eviction. Texture scopes retain uploaded inputs
only within a resource generation. Separate CPU and NPU contracts each check
480 components, six generations and twelve invalid uses. This comparison changes
both scoped upload and model cache settings; it does not isolate their individual
contributions. The earlier sixteen-request scope-only comparison is separate.

The renderer uses the qualified 4,096-sample generic batch size. Scalar uniform
lowering and graph fusion are excluded. Existing queue sharing, quad batching and
provider recovery remain enabled in both configurations. The next gate is a live
native trial of this exact worker, followed by matched Tahoma measurements.

[Verified report](report.json), [raw artifact manifest](raw-manifest.json), and
[source manifest](source-manifest.json) preserve 47 remotely hashed text artifacts
and the reproducible worker change. Desktop pixels, model weights and compiled
device caches are excluded.
