# Larger complex-shader batch: complete comparison

The corrected worker v29 preserves every recorded image and passes both bounded
provider-recovery tests. Aggregate parent rendering time is **0.9112% higher**
than worker v26 in the four-run comparison. It does not meet the five-percent
improvement threshold and is not promoted to the desktop.

This is the fresh qualification following the
[Windows servicing interruption](../native-complex-batch-interrupted-v1/README.md).
The worker binary is unchanged from the corrected attempt:
`68ee41f06082135693d0d8df748244d7ee1970030450e803cfbbcdbf5ef018a4`.
Only the complex shader's bounded transport batching changes relative to v26.
Kernel blobs, arithmetic and custom-import turbo settings remain unchanged.

## Correctness and timing

The original model passes eight exact post-update NPU checks. All 174 selected
requests, all 972 full-replay requests, and all 3,888 A/B/B/A comparison images
pass. Each comparison phase has one provider generation and no provider failure.

| Phase | Worker | Parent rendering time | Child rendering time |
| --- | ---: | ---: | ---: |
| A1 | 26 | 114,515.6606 ms | 102,067.4596 ms |
| B1 | 29 | 116,837.0363 ms | 104,430.8273 ms |
| B2 | 29 | 117,323.1159 ms | 104,879.3579 ms |
| A2 | 26 | 117,529.9885 ms | 105,217.8861 ms |

Across 1,944 matched request pairs, parent time increases from 232,045.6491 to
234,160.1522 ms. There are 950 slower individual pairs. These are two runs per
worker, not 1,944 independent trials. The small aggregate difference is an
observed result, not a statistically established regression.

The ten targeted complex-shader draws per replay use 2,412 provider calls across
the two baseline runs and 1,868 across the two candidate runs. Their aggregate
child time falls from 10,530.4583 to 9,773.0149 ms: **7.1929% lower**. These
intervals include host work and NPU waits. Fewer calls help the targeted route,
but do not demonstrate a full-replay or desktop benefit.

## Recovery and environment

Two thirty-image scenarios inject a provider exception and terminate the owned
child process, respectively. Each retries original ticket 668 on the NPU,
produces all thirty exact images, records two generations and one bounded retry,
and verifies larger complex batches. No CPU rendering fallback or real device
loss is claimed.

Both baseline and candidate run after the update, on Windows 26200.9711,
with NPU driver 32.0.100.5540 and GPU driver 32.0.101.8724. Device status is OK.
The one-off local task uses the same account in session zero, with BelowNormal
worker priority and the Balanced power scheme. Its lifetime is independent of
SSH. Pre-update timing is not used as a control. No interactive desktop or
Tahoma workload runs in this experiment.

## Evidence

`artifact-sha256.json` identifies 102 artifacts, including the runner, selected
and full replay, all four timed phases, recovery, environment capture and route
analysis. Remote source artifacts total 18,818,712 bytes. Large text files are
compressed losslessly, retaining stored and uncompressed hashes and lengths.
The unchanged worker's source and build provenance are in the preceding archive.
