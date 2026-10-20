# Windows resource boundaries: passing diagnostic V2

Delta's finite `native-live-v62` / desktop-admission V72 run passes, with the
physical desktop and ordinary windows restored afterward. This is an instrumented
diagnostic, not a faster renderer or a full-desktop-offload qualification.

The unchanged combined worker acknowledges 1,623 NPU operations in one provider
generation, with no fault or retry. All 103 physical submissions match their
source images; 90 source hashes are distinct. The shared test application
completes its eight NPU operations. The separate unleased adapter probe still
cannot create its default graphics device, so this does not establish general
application compatibility.

The 550-second presentation trial verifies forty controlled inputs. Warm
sent-input-to-display median/p95 is 113.335/283.284 ms for mouse and
104.047/116.788 ms for keyboard, with nineteen samples per kind. The 30 Hz
animation schedules 300 updates without producer skips but delivers seven
verified changes during ten seconds (0.7/s), nine including drain. Instrumentation
and separate runs preclude a speed comparison with the uninstrumented baseline.
There is no GPU-utilization or Tahoma result.

## Resource observations

V2 adds scopes for shader-view hazards, copy-region calls and device destruction.
The analyzer closes 6,009 calls across the observed processes and uniquely joins
549 completed worker requests. DWM contributes 2,321 closed calls, including 412
draws, 144 shader-view hazards and 65 copies. Its captured event prefix has no
serial gaps, incomplete calls or unscoped events. The indirect host has one
incomplete final call, retained explicitly. The capped traces are not complete
desktop histories or proof that every relevant callback has been covered.

There are 75 observed multi-draw runs containing 316 DWM draws, under the
assumption that intervening private input uploads retain immutable snapshots.
The largest run contains 39 draws and references 47 distinct texture versions,
totalling 48,376,472 logical bytes. That sum excludes target backgrounds, buffers
and padding; it is not measured residency or memory traffic. Only one multi-draw
run has a private target, no flagged external texture input and at most 64 KiB of
texture-version bytes. General textured batching is therefore still required.

No Windows callback defers rendering. Resource aliases, ownership, completion
fences, deadlines and submission behavior must be implemented before these
observations can become a correct queue.

## Recovery validation and evidence

The shared-application check now correlates the explicit provider generation and
child PID, while the provider-import audit enforces uniqueness within each child.
Their independent negative tests and the previous actual device-loss failure are
preserved in the [recovery evidence](../native-device-loss-recovery-v1/README.md).
This passing run has no device loss and does not explain that earlier fault.

`artifact-sha256.json` inventories 206 artifacts: 202 hash-verified remote files,
two local collection/preparation sources and the footprint analyzer/report.
Compiled binaries, model caches and pixel packets remain excluded. The driver
build, registration, physical delivery, input observations and restoration
reports are retained under `raw/`; CPU analyses remain explicitly separate from
hardware execution evidence.
