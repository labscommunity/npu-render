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

The implementation currently provides:

- Add/stop/remove and device/context lifetime bookkeeping.
- A versioned adapter identity query; it explicitly reports `ready = 0`.
- Bounded surface metadata validation and transactional allocation metadata
  creation. Allocation failure releases every partial object before returning.
  A dormant memory-segment implementation reserves and clears 64 MiB of actual
  contiguous system memory and reports its physical address through the two-pass
  WDDM 1.3 segment query. It rejects larger individual allocations. The startup
  gate still prevents this allocation on a live system: only its host model has
  run. Windows acceptance, paging, residency and NPU mapping remain unvalidated.
- A bounded FIFO state machine that distinguishes a completed Windows fence
  from the lifetime of a worker ticket. Stopping invalidates completions;
  restarting remains forbidden until an active worker separately acknowledges
  that it has drained. This state machine is not yet wired to kernel interrupts.
- A dormant paging builder for transfer, fill and discard operations in that
  owned segment. It writes pointer-free DMA markers and separate kernel-private
  records; allocation contents change only when the deferred executor runs.
  MDL page offsets and segment byte offsets are handled separately. The executor
  uses owned partial MDLs to preserve borrowed MDLs and release temporary mappings.
  Host tests cover both transfer directions, overlapping moves, deferred fills,
  invalid ranges, buffer exhaustion and mapping/allocation failure cleanup.
  Submission, completion, real MDL mappings and VidMm acceptance remain untested.
  Other paging operations reject explicitly; these failures are not valid normal
  behavior for a started adapter, so the startup gate remains essential.
- Explicit failing callbacks for unsupported rendering, presentation, paging operations,
  preemption and reset. Display callbacks are null.

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

Before enabling `StartDevice`, implement and test real segments and paging,
command submission and completion notifications, preemption, TDR/reset, worker
ownership/teardown, and all mandatory render DDIs/caps. A successful compiler,
host test, or even an eventual adapter enumeration is not graphics conformance.
After those contracts, normal adapter creation, shared-resource opening, NPU
execution, presentation, DWM selection, original GPU work retirement, and Tahoma
benefit each need separate end-to-end evidence.

The host DDI test replaces pool/contiguous allocation, physical-address lookup
and `DxgkInitialize` with test shims. It exercises descriptor validation,
memory clearing, two-pass segment queries, allocator failure at each position,
duplicate destruction, startup rejection, and cleanup. It cannot validate IRQL,
Windows scheduling, kernel memory mappings, driver signing, or PnP behavior.
The portable scheduler test compares 200,000 lifecycle operations against an
independent FIFO model, with explicit full-queue and delayed-completion cases.

The project deliberately does not use the Microsoft render-only sample's
registry changes that disable preemption and TDR, fake CPU physical aperture
address, or successful returns from unimplemented recovery operations.

References: [WDDM render-only requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps),
[preemption](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-preemption),
[segment reporting](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_querysegmentout3).
Paging contracts: [buffer construction](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer),
[offset units](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_buildpagingbuffer),
[partial MDL ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-iobuildpartialmdl).
