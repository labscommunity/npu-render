# Accounting for time outside rendering

This read-only analysis partitions the retained ten-second worker-19 desktop
animation interval. It performs no new device execution.

| Parent-worker interval | Time |
| --- | ---: |
| Request receipt to queue | 4.339 ms |
| Queue to execution entry | 0.184 ms |
| Execution entry to finish | 8,114.798 ms |
| Finish to reply | 196.323 ms |
| Reply to acknowledgment | 64.736 ms |
| Acknowledgment to next request receipt | 1,619.620 ms |

The intervals sum to exactly ten seconds within one microsecond of numerical
precision. The execution interval includes child rendering and parent work;
the earlier child-specific analysis attributes 6,839.935 ms to child execution.
These figures overlap and must not be added.

The largest gap before the next request is 49.794 ms. These timestamps do not
distinguish client copies, admission checks, DWM scheduling or transport.
The gaps motivate client-side measurement alongside kernel optimization. They
do not establish a cause, an achieved speedup or a future frame-rate bound.

The [report](report.json) pins both input artifacts by SHA-256 and retains the
twenty largest gaps. The [manifest](artifact-sha256.json) covers the analysis
source and report. The underlying live trial and its limitations remain in the
[desktop archive](../../2026-10-16/native-response-packet-live-v1/README.md).
