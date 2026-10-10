# Indirect-display admission laboratory

This driver provides one 1024×768 diagnostic monitor in the owned disposable
Windows guest. It discovers the paired laboratory render adapter through its
public KMT private-information contract, requests that adapter before announcing
the monitor, and records the actual render LUID supplied by Windows. It reads back
returned desktop surfaces through a staging texture and releases them. No viewer
or encoder runs inside the driver.

This answers whether Windows admits the experimental adapter for an indirect
desktop and how far swapchain creation progresses. A received surface is not
proof of NPU rendering, screen delivery, full desktop migration or Tahoma benefit.
The original display is retained. The driver does not change the primary monitor,
install itself, set graphics preferences for applications or alter the physical
host. Driver entry and device creation require `NR-GFX-LAB` / `NR_WINDOWS`.

The guarded installer must create `C:\npu-lab\idd-trace` with write access for the
UMDF host before use. Each host process records at most 4 MiB of JSON diagnostics:
requested and actual adapter identities, device creation status, frame dimensions,
and swapchain cleanup. `preferred_selected` is explicit when Windows chooses a
different adapter. No received frame is counted as displayed.

Each UMDF process also writes `latest-frame-<pid>.bin`, replacing the prior file
only after a complete readback has been written. Its 64-byte `FrameHeader` records
shape, RGBA/BGRA format, sequence and timestamp, followed by tightly packed pixels.
An incomplete write stays in `pending-frame-<pid>.bin`; it is never published as
a completed frame. Readers must use `FILE_SHARE_DELETE`. Storage is bounded by
two full frames per process. The shared immediate context is serialized across
consumer replacements. The WARP fixture validates changing pixel patterns,
padding removal, invalid-input rejection and replacement while an old reader is
open; it establishes no NPU execution or desktop delivery.

Build with `tools/build_indirect_lab.py --wdk-root <pinned-dependency-directory>`.
The builder extracts checksum-pinned UMDF 2.25 / IddCx 1.4 dependencies and creates
a source/binary hash report. It never signs or installs a driver. Installation and
removal are restricted to the disposable VM; this is not a deployable desktop mode.

`--system-renderer-control` builds an explicit control that omits the render-adapter
preference and leaves selection to Windows. Run it with the experimental render
device removed. The topology task's matching `-SystemRendererControl` flag checks
that prerequisite. This separates monitor activation from the custom renderer;
even a successful control establishes no NPU execution or desktop offload.

The lifecycle follows Microsoft's public
[IndirectDisplay sample](https://github.com/microsoft/Windows-driver-samples/tree/2dc3fd3a0cc84a2933f2194e7ec0871584979071/video/IndirectDisplay).
The selection contract is
[IddCxAdapterSetRenderAdapter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter):
it expresses a preference; the assign-swapchain callback reports the actual choice.

A failed-draw test exposed a lock cycle during cancelled assignment: the callback
joined its candidate frame thread while that thread called `WdfObjectDelete`
and waited for an IddCx lock held by the callback. The frame thread now hands
swapchain deletion to an independent framework work item before exiting. The
callback can finish its join without waiting for framework deletion. Work-item
creation failure returns the documented abandon-swapchain status before ownership
is accepted. This addresses the captured deletion/join cycle; installed-guest
teardown and successful frame delivery remain separate validation requirements.

The work item uses `AutomaticSerialization = FALSE`, is parented to the framework
device, owns only the swapchain handle after handoff, and deletes itself after
releasing that handle. Lifecycle references:
[framework work items](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nc-wdfworkitem-evt_wdf_workitem),
[assignment ownership and retry status](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nc-iddcx-evt_idd_cx_monitor_assign_swapchain).
