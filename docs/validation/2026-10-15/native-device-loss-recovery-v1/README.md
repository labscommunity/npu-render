# Real NPU device-loss recovery and finite native desktop qualification

The isolated renderer recovers from an actual NPU device loss and completes a
3,291-request recording with all 3,288 available reference hashes matching. Three
requests have no original successful reference; their execution and acknowledgments
pass. The subsequent native Windows trial passes 1,107 NPU operations, 35 matching
physical submissions, controlled mouse/keyboard input and exact ordinary recovery.
Full desktop offload remains unfinished: interaction is slow, 30-minute operation
and general application coverage are unqualified, and Tahoma has not been retested.

## Actual failure and recovery

Replay v39 uses worker v34 from the [provider isolation investigation](../native-provider-recovery-v1/README.md).
It repeats the complete 1,097-request live-v24 recording three times. Both fault
injection settings are disabled. At dispatch ticket 719, the first child records
`ZE_RESULT_ERROR_DEVICE_LOST` from a vertex inference. That inference takes
60.880 seconds. The supervisor retires the child, starts a fresh NPU process and
retries the complete immutable request. The caller receives one successful,
reference-matching output after 62.906 seconds. No CPU or GPU fallback occurs.
All remaining requests finish; there are two child generations and one retry.

The replay runs from 00:15:40.981 to 00:36:13.931 UTC on October 15. Its stopped
ETW trace reports 1,358,167 processed events and zero lost events. The selected
135,573 events belong to the supervisor's NPU children and contain one non-success
result, consistent with the recorded loss. API create/destroy balances are not
hardware allocation counts: terminated children do not execute all destruction
calls. This does not establish a resource leak or the underlying Intel driver bug.

This qualifies correctness of one real failure recovery. A roughly one-minute
stall is unacceptable for normal desktop interaction. Arbitrary failure recovery,
short bounded recovery latency and long-duration stability remain open.

## Native Windows path

Registered trial v22 passes eight NPU operations through the real Direct3D 11
runtime and installed diagnostic adapter. Temporary package and certificate trust
cleanup pass; memory integrity remains enabled.

Live v25 and v26 stop before driver admission because the ASUS OLED screen saver
ignores simulated input and normal window-close requests. Their missing native
cleanup reports reflect admission never starting. A first dismissal controller
incorrectly compares local and UTC time and rejects its evidence; the corrected
controller pins and terminates only the identified non-secure screen-saver process.
No login, screen-saver or display settings are changed. These failures are preserved.
Microsoft documents that [display-awake requests do not prevent screen savers](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-setthreadexecutionstate).

Live v27 then passes an 11-minute native observation, including a ten-minute
primary-topology interval. The isolated worker completes 1,107 operations and
acknowledgments, with one child and no provider failure. There are 37 native
frames, 35 matching physical submissions and 16 distinct source hashes.
A separate physical-output reader verifies blue, mouse-triggered green and
keyboard-triggered red while checking presenter ownership before and after reads.
Two pointer-confinement reacquisitions pass. Original topology, ordinary windows,
foreground state and cursor state restore, and independent cursor cleanup passes.

Mouse receipt to the first completed physical read of green is 20.645 seconds;
keyboard receipt to red is 0.567 seconds. These are two diagnostic observations,
not a latency distribution or matched performance benchmark. Request recording,
CPU trace analysis and observers are active. Physical-output duplication is not
an optical panel measurement. The presenter still performs uploads and GPU copies;
residual iGPU cost and Tahoma benefit remain unmeasured.

## Performance direction

The replay's successful child requests total 713.039 seconds of rendering and
896.354 seconds including preparation. They make 128,503 provider calls and
2,561 backend compilation/loading calls. Some individual draws make 1,282 provider
calls and take roughly 15 seconds. The failed 60.880-second inference is excluded
from these successful-request totals. Parent roundtrip timings include transport
and recovery and have a different scope.

Read-only analysis of the original stream finds 604 distinct referenced request
contents. A hypothetical 64 MiB exact-output cache would hit 196 of 1,096 referenced
requests, but those requests account for only 3.13% of the replay's successful
rendering-plus-preparation time. At 256 MiB the corresponding figures are 215 and
3.70%. This is an upper-bound analysis, not an implemented cache or measured speedup.
Larger dispatch batches and shared compiled models remain the next experiments.

## Evidence

All 143 collected text files, totaling 12,524,952 bytes, match hashes computed on
Delta. [report.json](report.json) contains checked assertions and derived timings;
[raw-manifest.json](raw-manifest.json) maps unmodified raw evidence. Worker source
v34 is unchanged from the previous archive. New controller and observer sources,
including failed attempts, are retained here. Pixels, compiled models, raw ETL,
signing keys and private addresses are excluded. The repository remains private.
