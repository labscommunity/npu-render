# Indirect-display admission laboratory

This driver provides one 1024×768 diagnostic monitor in the owned disposable
Windows guest. It discovers the paired laboratory render adapter through its
public KMT private-information contract, requests that adapter before announcing
the monitor, and records the actual render LUID supplied by Windows. It consumes
and releases returned desktop surfaces without displaying or encoding them.

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

Build with `tools/build_indirect_lab.py --wdk-root <pinned-dependency-directory>`.
The builder extracts checksum-pinned UMDF 2.25 / IddCx 1.4 dependencies and creates
a source/binary hash report. It never signs or installs a driver. Installation and
removal are restricted to the disposable VM; this is not a deployable desktop mode.

The lifecycle follows Microsoft's public
[IndirectDisplay sample](https://github.com/microsoft/Windows-driver-samples/tree/2dc3fd3a0cc84a2933f2194e7ec0871584979071/video/IndirectDisplay).
The selection contract is
[IddCxAdapterSetRenderAdapter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter):
it expresses a preference; the assign-swapchain callback reports the actual choice.
