# Windows kernel adapter bring-up

This is an original, **start-disabled** WDDM 1.3 kernel target. It is not a
registered rendering adapter, and it cannot move desktop work. `StartDevice`
returns `STATUS_NOT_SUPPORTED`; the existing display stays in control. No
installer or INF is shipped in the desktop bundle.

Build on x64 Windows with MSVC and the pinned SDK:

```powershell
uv run python tools/build_windows_driver.py
```

The script verifies and extracts the pinned WDK package, compiles a `.sys`
with `/kernel /W4 /WX`, and runs a user-mode test of the actual DDI source.
It does not sign, install, change boot settings, or register a device.
The optimized image includes CodeView information and a matching PDB for
isolated kernel debugging. The build report hashes both files; retain that
pair when test-signing the image so a debugger can verify symbol identity.
The driver target is rebuilt from clean objects because the remote MSBuild
project reused objects after included headers changed. Reports now include
source hashes and reject source changes during the build.

An opt-in diagnostic build observes the public `DxgkCbSynchronizeExecution`
callback during `StartDevice`:

```powershell
uv run python tools/build_windows_driver.py --lab-sync-probe --build-dir build/windows-kernel-sync-probe
```

It records the status, callback count, callback IRQL and Boolean result. Startup
still returns `STATUS_NOT_SUPPORTED`; the probe neither submits work nor signals
a completed fence. Default builds omit it. Host tests reject missing or repeated
callbacks, unsuitable IRQL, false callback results and non-success statuses.
Only a live kernel observation can establish Windows behavior.

The [October 9 isolated-guest run](../../../docs/validation/2026-10-09/kernel-sync/report.json)
of this probe succeeded: the public callback
returned `STATUS_SUCCESS`, invoked the routine exactly once at IRQL 3, and
returned its `TRUE` result. The driver then rejected startup and unloaded; the
original display remained healthy. This validates the synchronization route on
that Windows build, without a hardware interrupt resource. It does not validate
`DxgkCbNotifyInterrupt`, deferred callbacks, fence completion or portability to
other builds. Those require actual submitted work and separate observations.

`--lab-worker-probe` additionally runs a finite system thread that fills an owned
segment and transfers 1,024 patterned bytes to and from an owned nonpaged MDL,
checking deferred execution, offset units, guard bytes and the borrowed MDL
header. After releasing the memory it requests a Dxgkrnl DPC through the public
synchronization callback. The parent joins the thread and drains queued DPCs
before rejecting startup. Neither `NotifyInterrupt` nor `NotifyDpc` is called:
there is no Windows-submitted fence to complete. This diagnostic is omitted
from default builds. Its host model checks failed allocations, failed thread
creation, rejected callbacks, a late DPC and a retried join; it does not model
concurrency or establish live Windows memory-manager acceptance.

The [first worker run](../../../docs/validation/2026-10-09/kernel-worker-v7/report.json)
created and joined a real kernel thread, but returned
`STATUS_INSUFFICIENT_RESOURCES` during the memory exercise before requesting a
DPC. The original display remained healthy and the test package was removed.
An [instrumented rerun](../../../docs/validation/2026-10-09/kernel-worker-v8/report.json)
isolated the failure to the contiguous backing allocation.
The worker diagnostic now explicitly tries 64 MiB, 16 MiB, 1 MiB and 64 KiB,
stopping at the first successful allocation and reporting its actual size and
the first attempt's status. A reduced diagnostic allocation does not establish
desktop capacity. The normal startup path still requests 64 MiB.

The [sized worker run](../../../docs/validation/2026-10-09/kernel-worker-v9/report.json)
passed with 16 MiB after the 64 MiB request failed. It verified the fill and
round trip through real partial MDL mappings, then observed the synchronization
callback at IRQL 3 and the actual DPC at IRQL 2. The thread joined, queued
callbacks drained, the module unloaded and the original display remained healthy.
This validates the owned-memory worker and DPC route on that guest. Windows
submission acceptance, completed OS fences, NPU execution and desktop offload
remain untested by this diagnostic.

`--lab-engine-probe` exercises the new queue worker with real kernel threads,
public synchronization and actual DPC routing. It requests a 16 MiB lab segment
and the queue depth Windows specifies, then checks a deferred fill, idle
preemption, thread shutdown and restart. Its test completion/preemption records
terminate at driver-owned observers. It never forwards artificial fence values
to Windows' `NotifyInterrupt` or `NotifyDpc` callbacks and still rejects startup.
This diagnostic can establish kernel concurrency behavior, not Windows scheduler
acceptance. It is separately opt-in and omitted from default builds.

The [October 9 registration trace](../../../docs/validation/2026-10-09/kernel-registration/report.json)
validated the earlier `288be1a` baseline in an isolated Windows guest. Windows
called `DriverEntry`, `AddDevice`, `StartDevice` and `RemoveDevice`; the first,
second and fourth succeeded, while startup returned its intended rejection.
The module unloaded, the test devnode and package were removed, and the existing
display remained healthy. This does not validate the newer memory/paging code,
an operational graphics adapter, NPU execution or desktop migration.

The implementation currently provides:

- Add/stop/remove and device/context lifetime bookkeeping.
- A versioned adapter identity query; it explicitly reports `ready = 0`.
- Bounded surface metadata validation and transactional allocation metadata
  creation. Allocation failure releases every partial object before returning.
  A dormant memory-segment implementation reserves and clears 64 MiB of actual
  contiguous system memory and reports its physical address through the two-pass
  WDDM 1.3 segment query. It rejects individual allocations above the actual
  reserved capacity. Smaller page-aligned capacities are available to the
  explicit worker diagnostic. Normal builds keep allocation behind the startup
  gate. Windows acceptance, paging, residency and NPU mapping remain unvalidated.
- A bounded FIFO state machine that distinguishes a completed Windows fence
  from the lifetime of a worker ticket. Stopping invalidates completions;
  restarting remains forbidden until an active worker separately acknowledges
  that it has drained. DMA-boundary preemption waits for the active buffer,
  cancels the older queue prefix and preserves submissions after the request.
  The dormant engine serializes queue changes through the public synchronization
  callback. Live Windows submission and notification acceptance remain untested.
- Preallocated submission storage sized from Windows' `RequiredDmaQueueEntry`,
  with an additional active-worker slot and an explicit 64 MiB storage budget.
  An excessive requirement rejects startup instead of silently reducing queue
  depth. Kernel-private paging records are copied into owned slots before the
  submitter returns; capture does not allocate or execute work. Host tests cover
  allocation failure, malformed records, partial-buffer offsets, input mutation,
  deferred execution, ring reuse, empty context switches and drain-before-free.
  This storage feeds `DxgkDdiSubmitCommand` only after adapter startup; the live
  startup gate remains closed.
- A dormant paging builder for transfer, fill and discard operations in that
  owned segment. It writes pointer-free DMA markers and separate kernel-private
  records; allocation contents change only when the deferred executor runs.
  MDL page offsets and segment byte offsets are handled separately. The executor
  uses owned partial MDLs to preserve borrowed MDLs and release temporary mappings.
  Host tests cover both transfer directions, overlapping moves, deferred fills,
  invalid ranges, buffer exhaustion and mapping/allocation failure cleanup.
  The isolated worker validates real partial MDL mappings on owned memory.
  Windows submission, fence completion and VidMm acceptance remain untested.
  Other paging operations reject explicitly; these failures are not valid normal
  behavior for a started adapter, so the startup gate remains essential.
- A dormant paging engine with an owned system thread, completion/fault
  notifications, DPC notification, current-fence queries, DMA-boundary preemption,
  and adapter timeout reset/restart. It joins the worker and drains DPCs before
  releasing storage; failures do not produce successful completion notifications.
  Host tests manually step the actual engine through callback shims, including
  a failed completion synchronization, coalesced DPCs, reset cleanup and restart.
  After an adapter reset drains the worker, it advances the scheduling baseline
  to the last submitted fence as WDDM requires. This does not emit a completion
  notification or claim the cancelled work executed.
  These tests do not establish concurrency, Windows scheduler acceptance or
  valid per-engine TDR. The current queue admits one pending preemption request;
  the OS behavior at that boundary still needs validation.
- Explicit failing callbacks for unsupported rendering, presentation, paging
  operations and per-engine recovery. Display callbacks are null.

The separate `nr_render_umd` target uses the existing restricted renderer
without the software laboratory's KMT replacement exports. Its `OpenAdapter`
requires the paired kernel identity, ABI and readiness response. The DLL load
contract test supplies controlled callbacks; it is **not** evidence of Windows
loading the DLL through a registered DXGI adapter. This UMD still accepts only
the existing laboratory shader and restricted state subset.

`nr_registered_probe --list` lists the available DXGI adapter LUIDs. After a
usable paired adapter exists, run `nr_registered_probe PATH_TO_UMD CPU|GPU|NPU
report.json HIGH:LOW` to exercise the same pixel oracle through normal
`D3D11CreateDevice` adapter selection. The probe queries the kernel identity and
readiness before creating a device. It preloads the specified UMD for backend
configuration and requires execution statistics from that exact module, with
zero software KMT replacement calls. An absent, foreign, or unready adapter
fails; there is no default-adapter or WARP retry. Current validation covers
rejection only, since the kernel startup gate remains closed.

Before enabling `StartDevice`, validate the paging engine in the isolated kernel
and finish per-engine recovery and all mandatory render DDIs/caps. Submission,
notifications, preemption and reset still need actual Windows-scheduler evidence.
A successful compiler,
host test, or even an eventual adapter enumeration is not graphics conformance.
After those contracts, normal adapter creation, shared-resource opening, NPU
execution, presentation, DWM selection, original GPU work retirement, and Tahoma
benefit each need separate end-to-end evidence.

The host DDI test replaces pool/contiguous allocation, physical-address lookup
and `DxgkInitialize` with test shims. It exercises descriptor validation,
memory clearing, two-pass segment queries, allocator failure at each position,
duplicate destruction, startup rejection, and cleanup. It cannot validate IRQL,
Windows scheduling, kernel memory mappings, driver signing, or PnP behavior.
The portable scheduler test compares 800,000 lifecycle operations across four
queue capacities against an independent FIFO model, with explicit full-queue,
delayed-completion and external-storage lifetime cases.

The project deliberately does not use the Microsoft render-only sample's
registry changes that disable preemption and TDR, fake CPU physical aperture
address, or successful returns from unimplemented recovery operations.

References: [WDDM render-only requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps),
[preemption](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption),
[segment reporting](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout3).
Paging contracts: [buffer construction](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer),
[offset units](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_buildpagingbuffer),
[partial MDL ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iobuildpartialmdl).
Submission contracts: [required preallocation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/dispmprt/ns-dispmprt-_dxgk_start_info),
[private-data ranges and empty submissions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_submitcommand).
Engine contracts: [interrupt notification](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkcb_notify_interrupt),
[preemption](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_preemptcommand),
[reset synchronization and zero-byte eviction](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/thread-synchronization-and-tdr).
[Reset fence semantics](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8)
require distinguishing recovery baselines from successful execution.
