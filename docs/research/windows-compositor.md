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
track remains separate: no system-wide renderer is installed here.

The new [Direct3D DDI laboratory](../../native/windows/ddi/README.md) implements
a bounded command stream through the real Direct3D runtime, described below. The Microsoft sample checkout examined
for this work is pinned to `de4a2161991eda254013da6c18226f5ea06e4a9c`.
Its render-only implementation has VC4 resource layouts, shader translation and
hardware MMIO assumptions. Copying or renaming it would not provide a Panther Lake
backend. Driver design must first define surface ownership, fence translation,
per-operation coverage and explicit unsupported-command behavior; then validate
an NPU-backed command executor before isolated-display DWM tests.

The broad GPU-to-NPU objective remains open. Neither the bounded handoff nor
Direct3D device-creation probing establishes whole-desktop offload.

## Direct3D command execution on the NPU

On October 8, an explicit test client successfully created a Direct3D 11 device
using our DLL and executed admitted draw calls on Delta's NPU. Microsoft's
[`D3D11CreateDevice`](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-d3d11createdevice)
accepts an explicit software-rasterizer module. The runtime negotiates the
[D3D10 user-mode driver interface](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d10ddi_openadapter)
and dispatches commands into that DLL. This supplies a testable graphics-command
boundary without installing a system driver. `SOFTWARE` describes how Direct3D
loads the DLL; admitted draws then run on the explicitly selected OpenVINO device.

The implementation recognizes one exact fullscreen vertex/pixel shader pair and
maps source-over draws to the existing NPU blend graph. It retains host-backed
resources and accounts for uploads, updates, copies and inference transfers.
Unsupported shaders and shared resources fail explicitly. It is not a general
shader translator, conformant feature-level-10 renderer, DWM adapter or display
path. See the [contract and reproducible checks](../../native/windows/ddi/README.md)
and [source/package evidence](../validation/2026-10-08/windows-ddi/README.md).

The process-local adapter thunks are adapted from
[Mesa 25.3.0](https://gitlab.freedesktop.org/mesa/mesa/-/blob/mesa-25.3.0/src/gallium/frontends/d3d10umd/D3DKMT.cpp),
revision `f7aeb0d677e71b68bf2ea04b07c0b7274451deab`, with notices retained.
Their software LUID and WDDM metadata are not a hardware NPU identity. WDK headers
come from a checksum-pinned NuGet dependency, without installing/registering a
driver. Microsoft documents this distribution mechanism in
[Install the WDK using NuGet](https://learn.microsoft.com/en-us/windows-hardware/drivers/install-the-wdk-using-nuget).

This resolves one uncertainty: ordinary Direct3D commands can reach an NPU executor
through a driver interface. It does not resolve transparent Windows integration.
DWM does not load an arbitrary laboratory DLL because another application selected
it. The normal [user-mode display-driver loading path](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/loading-a-user-mode-display-driver)
is associated with the installed graphics-driver configuration. Registering that
name alone would still leave all required render DDIs, shared allocations, fences,
shader semantics, presentation and recovery unimplemented.

The next kernel/user-mode boundary must preserve real shared-resource identities
and ownership across the application, composition and display devices. Host file
mapping handles are not interchangeable with WDDM allocation handles. The current
shared-texture rejection is an implementation gap demonstrated by the test, not a
proof that an NPU graphics driver is impossible. Until that boundary and actual
DWM selection work, whole-desktop offload remains unfinished.

For that allocation milestone, Microsoft's
[`D3DKMTCreateAllocation` contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtcreateallocation)
connects UMD allocation descriptions to the KMD and video-memory manager. Its
standard existing-system-memory allocation path requires shared/cross-adapter
flags; it is a candidate to investigate, not a facility implemented by our
software thunks. The [hybrid resource model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-cross-adapter-resources-in-a-hybrid-system)
also defines layout/alignment and driver capability requirements. The next proof
must therefore use real OS allocations opened by another graphics device, with
coherent ownership transitions. Our earlier D3D12/NPU buffer experiment establishes
interoperability for its application but does not provide this D3D11 driver/DWM
resource path.

## October 8 capture and memory follow-up

The [captured-window experiment](../../native/windows/capture/README.md) reaches
native-display output and preserves mouse/keyboard input to the original windows.
It does not solve the required work-retirement boundary: client rendering and
GPU capture remain, and isolated window pixels do not reproduce all original
Windows backdrop/shadow effects. Passing the independent output-image oracle is
separate from passing the original-desktop oracle; the latter currently fails.
Capture is retained as an experimental diagnostic, not the full-desktop solution.

The [real shared allocation probe](../../native/windows/wddm-memory.md) resolves
a lower-level uncertainty. Delta's NPU KMD accepts standard section-backed VidMm
allocations and shares them across adapters/processes. A child process imports
those handles and runs NPU composition; a GPU resource over the section consumes
the changing output without a CPU result upload. ExistingSysMem succeeded in one
process but failed to open in the child; ExistingSection passed. Host events and
GPU completion waits still coordinate ownership. This is not a registered graphics
adapter, D3D11 shared texture, native NPU/GPU shared fence or DWM selection.

The next useful driver work must connect these resources to graphics-DDI resource
identities and metadata, then execute the real composition command stream. The
stock NPU's ability to allocate/import memory does not supply those graphics DDIs.
An IDD or capture layer alone still cannot retire all existing composition work.

## Shared Direct3D resource boundary follow-up

The separate `nr_ddi_shared_lab` now creates real legacy-shared allocations through
the runtime callbacks. Another Direct3D process opens them, executes the admitted
shader pair on NPU remote inputs and returns changing pixels. The creator can
release/reopen its resources while the worker holds them. Full-resolution checks
require independent pixel oracles and complete mapping/alias cleanup.

The stronger native-Arc consumer check fails even though `OpenSharedResource`
returns success: GPU reads remain zero, and writing through that imported texture
also fails. An ordinary GPU-created texture passes the same write/read control.
The allocation-owner differential (NPU versus GPU KMT owner) does not fix the
result. This is evidence of an unresolved graphics-resource representation or
usage boundary, not proof of successful cross-driver texture interoperability.

See [implementation and reproducible commands](../../native/windows/ddi/README.md#shared-resource-experiment)
and [retained hardware evidence](../validation/2026-10-08/ddi-shared/README.md).
The installed desktop renderer is unchanged. A new selectable render adapter and
DWM's actual shader/state coverage remain required; passing the same-driver
resource round trip does not make that system-level integration complete.
