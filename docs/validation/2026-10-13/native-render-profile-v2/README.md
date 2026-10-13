# Corrected combined NPU rendering profile

The corrected worker reproduced all 1,438 recorded desktop requests exactly on
Delta's NPU, with 1,438 acknowledged replies and no worker errors. Its runtime
telemetry confirmed the requested 16/48/4 FIFO pipeline capacities, compiled
program reuse, compact coverage and the private shared queue property. This
corrects the configuration defect preserved in [v1](../native-render-profile-v1/).

The replay took 388,210.636 ms (6 minutes 28 seconds). Combined setup and draw time
was 351,364.596 ms. There were 1,194 compiled-program cache hits and 581 backend
calls, all loaded from the disk cache. Compact coverage handled 13,992 calls and
51,063,808 samples; 2,767 calls used the wide NPU coverage path. Receipt fields
matched the reference except for the exact expected eight additional logical
input bytes per compact coverage sample.

This is one corrected combined run without a matched performance control. It
establishes correctness and the actual configuration, not a percentage speedup
against earlier builds. Setup and draw timers are host measurements; they do not
measure hardware queue count or pure NPU kernel time.

The clean Windows build passed 32 CPU contract suites and five real NPU component
stages. The replay harness passed 107 reference/configuration boundary checks.
Eighteen archived raw artifacts were checked against their remote SHA-256 and
size before compression. The source patch is relative to the preserved v1
experiment; the source manifest identifies the complete input tree.

The worker accepts four local clients and serializes provider access. This test
replays previously captured operations; it does not migrate Delta's physical
desktop, establish usable interactive latency, or measure Tahoma performance.
Production runtime defaults are unchanged.
