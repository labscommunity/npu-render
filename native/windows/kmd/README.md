# Windows kernel adapter bring-up

This is an original WDDM 1.3 kernel target whose **default build is
start-disabled**. It is not a usable rendering adapter and cannot move desktop
work. Default `StartDevice` returns `STATUS_NOT_SUPPORTED`. The separate
disposable-VM paging mode below can attempt startup without enabling rendering
or display ownership. No installer or INF is shipped in the desktop bundle.

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

`--lab-paging-adapter` is a separate, explicit **disposable kernel-debug VM only**
build. It attempts startup with a 16 MiB software paging segment and Windows'
required queue depth, using actual scheduler callbacks for actual submissions.
It cannot be combined with synthetic startup probes. It exposes zero display
sources/children, keeps the paired UMD identity `ready = 0`, and provides no
rendering/presentation implementation. This mode exists to test Windows memory
manager and scheduler contracts before enabling normal rendering. The build
report records `start_enabled = true`, `lab_paging_adapter_enabled = true`, and
`rendering_ready = false`; building neither installs nor signs it.

Host tests cover its successful startup and allocation/thread failure unwind.
These tests do not establish that Windows accepts the reported capabilities,
submits paging work, or accepts completion notifications. That requires a live
guest run. Do not install this mode on the physical development machine.

The [first live paging-mode attempt](../../../docs/validation/2026-10-09/kernel-paging-v18/report.json)
returned success from `StartDevice` with 16 MiB and 33 queue slots. Windows then
queried capabilities, rejected the adapter with Code 43, and unloaded it. The
public KMT probe found no matching adapter and created no resources. No segment
query, actual submission or OS fence completion was observed. Removal preserved
the original display. The specific rejected capability remains under investigation.

The [version-consistent retry](../../../docs/validation/2026-10-09/kernel-paging-v19/report.json)
also failed. Stepping the callback and its Windows caller showed successful
capability output followed by `DXGADAPTER::Initialize` returning
`STATUS_OBJECT_NAME_NOT_FOUND`. The exact missing object was not traced; the
test INF lacked the required user-mode driver registration. The next packaging
experiment supplies the explicitly rejecting `nr_paging_guard.dll` and its
per-device registry entries while retaining the same kernel binary. This is
still a paging experiment, not a registered Direct3D renderer.

The [first live engine run](../../../docs/validation/2026-10-09/kernel-engine-v12/report.json)
passed: Windows requested 32 queue entries, the driver reserved 33 slots, and
the worker completed the fill, idle preemption and restart checks. The observer
received three records through real DPC deliveries at IRQL 2; the worker ran at
IRQL 0 and the synchronized observer at IRQL 3. Cleanup joined the worker,
unloaded the module and removed the package while preserving the original
display. Actual Windows DMA submission and GPU-scheduler notification acceptance
remain untested.

The [October 9 registration trace](../../../docs/validation/2026-10-09/kernel-registration/report.json)
validated the earlier `288be1a` baseline in an isolated Windows guest. Windows
called `DriverEntry`, `AddDevice`, `StartDevice` and `RemoveDevice`; the first,
second and fourth succeeded, while startup returned its intended rejection.
The module unloaded, the test devnode and package were removed, and the existing
display remained healthy. This does not validate the newer memory/paging code,
an operational graphics adapter, NPU execution or desktop migration.

The implementation currently provides:

- Add/stop/remove and device/context lifetime bookkeeping.
- Dormant allocation description and per-device open/close callbacks. Opens
  resolve Windows' 32-bit allocation handles through `DxgkCbGetHandleData`,
  preserve read-only intent, and publish handles only after every allocation
  succeeds. Open references prevent allocation/device teardown. Host tests
  cover cross-device opens, invalid ownership, partial allocation failure,
  duplicate closes and metadata output. Actual Windows opens remain untested.
- A versioned adapter identity query; it explicitly reports `ready = 0`.
- Capability reporting for one copy node, DMA-boundary preemption and per-engine
  recovery. It reports no graphics aperture, GDI acceleration, display flips,
  cross-adapter resources or runtime power components. The broader WDDM feature
  table and individual capability docs differ in their applicability to this
  software paging prototype; no unsupported bits are set merely to pass startup.
  The capability version matches `DriverEntry`'s WDDM 1.3 version, following the
  [Microsoft render-only sample](https://github.com/microsoft/graphics-driver-samples/blob/master/render-only-sample/roskmd/RosKmdAdapter.cpp).
  This differs from the member reference's instruction to return zero for newer
  interfaces. Both zero-version and version-consistent attempts failed after the
  capability query; the latter reported a missing object during initialization.
  The version change alone did not resolve that failure.
  Windows acceptance remains an experimental question. Power notifications retain
  the software engine's nonpaged RAM and worker state, which follow system power;
  there are no device registers or separate powered memory to restore. This is
  not NPU power management and must be extended before an NPU provider is used.
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
  default startup gate remains closed.
- A dormant paging-only patch callback using the same private-record validation
  as submission. It checks the submitted marker slice without executing or
  queueing it. Paging addresses were resolved during buffer construction and
  have no relocation entries; completion uses the later submission fence.
  Host tests cover partial slices, repeated patching, malformed markers and
  metadata, empty packets, and preservation of queue/memory state. Normal
  rendering relocation remains unsupported; live Windows patching is untested.
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
  behavior for a general rendering adapter. Only the explicitly restricted
  paging-mode guest experiment may exercise the incomplete adapter.
- A dormant paging engine with an owned system thread, completion/fault
  notifications, DPC notification, current-fence queries, DMA-boundary preemption,
  and adapter timeout reset/restart. It joins the worker and drains DPCs before
  releasing storage; failures do not produce successful completion notifications.
  Host tests manually step the actual engine through callback shims, including
  a failed completion synchronization, coalesced DPCs, reset cleanup and restart.
  After an adapter reset drains the worker, it advances the scheduling baseline
  to the last submitted fence as WDDM requires. This does not emit a completion
  notification or claim the cancelled work executed.
  These host tests do not establish concurrency or Windows scheduler acceptance.
  The current queue admits one pending preemption request;
  the OS behavior at that boundary still needs validation.
- Dormant per-engine recovery callbacks for the single node. Node reset joins
  the worker, identifies the active aborted packet (or last completed packet),
  resets the scheduling baseline, and starts an empty queue before succeeding.
  Host tests distinguish active work from later queued work, replay unaffected
  paging fences, preserve faults and escalate a failed restart to adapter reset.
  Progress queries report unresponsive for failed synchronization, a terminated
  worker or outstanding work with no progress for one second. Debug collection
  uses individually atomic observations without allocating, waiting or calling
  Dxgkrnl; it does not promise a simultaneous snapshot across fields. Actual
  Windows-initiated per-engine TDR remains untested.
- Explicit failing callbacks for unsupported rendering, presentation, paging
  operations and remaining adapter capabilities. Display callbacks are null.

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

Before enabling normal rendering, validate the paging engine in the isolated kernel
and finish all mandatory render DDIs/caps. The separate paging-mode guest experiment
does not satisfy that requirement. Submission,
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
