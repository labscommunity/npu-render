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

The implementation currently provides:

- Add/stop/remove and device/context lifetime bookkeeping.
- A versioned adapter identity query; it explicitly reports `ready = 0`.
- Bounded surface metadata validation and transactional allocation metadata
  creation. Allocation failure releases every partial object before returning.
  These descriptors do **not** yet have a working VidMm segment or paging path.
- A bounded FIFO state machine that distinguishes a completed Windows fence
  from the lifetime of a worker ticket. Stopping invalidates completions;
  restarting remains forbidden until an active worker separately acknowledges
  that it has drained. This state machine is not yet wired to kernel interrupts.
- Explicit failing callbacks for unsupported rendering, presentation, paging,
  preemption and reset. Display callbacks are null.

The separate `nr_render_umd` target uses the existing restricted renderer
without the software laboratory's KMT replacement exports. Its `OpenAdapter`
requires the paired kernel identity, ABI and readiness response. The DLL load
contract test supplies controlled callbacks; it is **not** evidence of Windows
loading the DLL through a registered DXGI adapter. This UMD still accepts only
the existing laboratory shader and restricted state subset.

Before enabling `StartDevice`, implement and test real segments and paging,
command submission and completion notifications, preemption, TDR/reset, worker
ownership/teardown, and all mandatory render DDIs/caps. A successful compiler,
host test, or even an eventual adapter enumeration is not graphics conformance.
After those contracts, normal adapter creation, shared-resource opening, NPU
execution, presentation, DWM selection, original GPU work retirement, and Tahoma
benefit each need separate end-to-end evidence.

The host DDI test replaces only pool allocation and `DxgkInitialize` with test
shims. It exercises descriptor validation, allocator failure at each position,
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
