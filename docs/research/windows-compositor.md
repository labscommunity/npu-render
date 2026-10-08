# Moving the operating system's rendering

The target includes Windows' existing desktop composition. Rendering another
application window on the NPU does not establish that this workload moved.
The October 7 [desktop measurements](../validation/2026-10-07/desktop/README.md)
separate those questions and retain actual DWM process/adapter counters.

## What must move

Applications produce window surfaces. Windows' composition engine combines them
and applies effects; display hardware scans out the result. Microsoft's
[DirectComposition architecture](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components)
places the composition engine in `dwmcore.dll`, hosted by `dwm.exe`. Moving that
calculation can release GPU resources without moving the monitor connection.
Application rendering inside each surface is another boundary.

Composition math is a plausible NPU workload: texture lookup, transforms, color
operations and source-over blending. This repository has measured parts of that
math on Delta's NPU. A replacement must receive surfaces before the existing
renderer performs those operations, produce equivalent pixels, and retire that
existing work. Processing a captured, finished desktop would not satisfy this.

The display staying on does not imply continuous shader rendering of every pixel.
Windows can use direct/independent flip and hardware overlay planes; DWM can sleep
between relevant changes. Measure idle and changing desktops separately and retain
useful display hardware paths.
[Microsoft's flip-model guidance](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/for-best-performance--use-dxgi-flip-model).

## Windows integration candidates

| Boundary | What it provides | What remains necessary |
|---|---|---|
| Application provider | Control over that application's pixel production; demonstrated here | Existing-application integration; does not replace DWM |
| Indirect display driver (IDD) | Virtual/indirect monitor, completed desktop surfaces, preferred render-adapter selection | A graphics renderer must still produce the desktop; postprocessing does not remove composition |
| NPU-backed WDDM graphics render adapter | Proposed boundary to accept graphics commands and translate work to NPU execution | Graphics DDIs, shaders/formats, resource sharing, synchronization, recovery, DWM compatibility, signed deployment and performance |
| Linux compositor renderer | Existing renderer boundary; restricted Weston CPU integration works | Native NPU buffer import/output and DRM presentation; client rendering remains separate |

Microsoft's [IDD overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/indirect-display-driver-model-overview)
describes receiving the desktop image as a DirectX surface.
[IddCxAdapterSetRenderAdapter](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter)
sets a preferred render adapter by LUID; Windows reports the actual selected
adapter and may choose another. An IDD cannot make a graph-only device graphics
compatible merely by naming it. Its selection also applies to the indirect
display, not automatically to the laptop's existing internal panel.

An NPU-backed Windows graphics adapter is an engineering hypothesis, not ruled
out by the absence of a stock switch. It is substantially more than an IDD:
[WDDM requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/wddm-driver-and-feature-caps)
require render-specific DDIs for a graphics render-only driver.
[MCDM terminology](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/mcdm)
also uses “render-only” for compute devices, which does not imply graphics support.
The new [direct-LUID probe](../validation/2026-10-07/desktop-delegate/README.md)
finds Delta's NPU through DXCore, but Direct3D 12 device creation fails with
`DXGI_ERROR_UNSUPPORTED` for graphics, core-compute and generic-ML feature levels.
The exact GPU adapter succeeds. The NPU is absent from DXGI graphics enumeration.
This corrects the narrower older filtered-enumeration result: the adapter exists,
but the installed driver exposes no working tested Direct3D route. NT-handle
sharing and successful NPU blending do not supply those graphics interfaces.

Microsoft's [graphics driver samples](https://github.com/microsoft/graphics-driver-samples)
are references, not a ready Panther Lake solution. Their render-only sample
targets different hardware and has limited Direct3D coverage. No sample driver
was installed or substituted for Delta's driver.

## Gates for a real Windows switch

1. Create the required Direct3D devices and correctly execute a bounded composition
   command stream on the NPU. Adapter enumeration alone is insufficient.
2. Prove formats, shared-surface ownership, fences and cross-adapter presentation
   with changing input. Record all retained CPU/GPU work.
3. Validate DWM compatibility and actual adapter selection on an isolated display,
   including recovery. A virtual-monitor result does not prove internal-panel routing.
4. Replay identical desktop activity with/without offload. Measure correctness,
   presentation cadence, GPU engines, Tahoma, and shared memory/power costs.
5. Ship the switch for validated boundaries and operations. Games, codecs and
   arbitrary GPU compute retain separate coverage gates.

## Implemented intermediate boundary

`nr_desktop_delegate` now hands composition of three cooperating GDI windows
between DWM and an explicit NPU/GPU compositor. Their original HWNDs are hidden
only after the replacement is ready, and restored on return to DWM. All modes
pass the same full-image and on-screen pixel oracles. This establishes a bounded
handoff that removes those original surfaces from DWM's visible scene. The outer
canvas remains a DWM surface; unrelated windows and client rendering do not move.
See the [handoff and concurrent-inference evidence](../validation/2026-10-07/desktop-delegate/README.md).

Extending that boundary requires cooperating applications to provide surfaces,
damage, ordering and lifecycle events before ordinary composition. A screen-capture
API supplies pixels after rendering and cannot by itself retire the source work.
For transparent use by arbitrary existing Windows applications, the graphics-driver
track remains separate: no such renderer is installed or implemented here.

The next driver artifact must implement and test a bounded graphics command stream,
not just enumerate a synthetic adapter. The Microsoft sample checkout examined
for this work is pinned to `de4a2161991eda254013da6c18226f5ea06e4a9c`.
Its render-only implementation has VC4 resource layouts, shader translation and
hardware MMIO assumptions. Copying or renaming it would not provide a Panther Lake
backend. Driver design must first define surface ownership, fence translation,
per-operation coverage and explicit unsupported-command behavior; then validate
an NPU-backed command executor before isolated-display DWM tests.

The broad GPU-to-NPU objective remains open. Neither the bounded handoff nor
Direct3D device-creation probing establishes whole-desktop offload.
