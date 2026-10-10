# Registered Direct3D submission laboratory

This guest-only UMD connects ordinary D3D11 texture clears to the private fill
command exercised by the [v23 kernel test](../../../docs/validation/2026-10-09/kernel-paging-v23/report.json).
It uses the normal registered-adapter loader ABI, real runtime allocation/context
callbacks, and no process-local KMT replacement exports. It does not execute NPU
work in its clear/copy path, presentation or desktop composition. A new restricted
draw path connects an explicit composition provider. Negotiating feature level
10.0 for this restricted test does not establish conformance.

`nr_registered_clear_probe --exercise` selects exactly one paired adapter by KMT
identity and DXGI LUID, creates a D3D11 device on that adapter, and performs three
clears each in BGRA8 and RGBA8. It checks every returned pixel through staging
readback, verifies the expected UMD was loaded, and checks cleanup counters. Both
the probe and UMD refuse to operate outside the owned `NR-GFX-LAB` guest with the
`NR_WINDOWS` volume. The production UMD's readiness gate remains closed.

The admitted operations are 2D render targets, staging textures, clears,
staging copies, and staging read maps. Resource data uses VidMm
allocations with no private pixel-buffer fallback. Clears submit immediately and
replace all command-buffer pointers with the runtime's returned pointers. Failed
submission invalidates the context's writable command pointers. Unsupported
operations report an error to the runtime. A new `--shared` probe exercises
legacy shared render targets between two devices on this same adapter, including
creator release/reopen and alternating writers. Their allocation and resource
metadata must agree, and each import is released through its own runtime resource
identity. `--shared-nt` uses NT handles and keyed mutexes between two devices.
`--shared-process` and `--shared-process-nt` run separate child processes, release
the creator's original resource, reopen it, and verify writes in both directions.
The NT test closes the creator's original handle and duplicates the child's
retained handle back into the parent. Child processes inherit only their explicit
standard I/O handles and, for NT sharing, the single resource handle. Every process
checks its own cleanup counters. Cross-adapter sharing, arbitrary usage,
general shader execution and display operations remain unsupported.

The restricted draw path admits the existing exact fullscreen vertex/pixel shader
pair and premultiplied source-over or source-copy blending. It validates geometry,
viewport, state and resource ownership before executing a separately implemented
OpenVINO provider. Configuration is explicit (`NPU` by default; `CPU` and `GPU`
are controls), is rejected while devices are alive, and never permits `AUTO` or
fallback. Every compiled graph's execution-device property is checked. Host
readbacks, input copies and result uploads remain explicit and counted; the NPU
does not own Windows' allocation callbacks or execute the kernel copy worker.

`nr_registered_lab_test NPU` calls the actual UMD entrypoints with test-only
allocation callbacks and runs the real NPU provider. It checks 16 draws, retained
bindings, unsupported shader/state rejection and cleanup. This is a component
test, not evidence that Windows selected an installed NPU graphics adapter.
`nr_registered_clear_probe --draw-cpu` and `--draw-npu` exercise the normal
registered loader in the guest. They cover two formats, two widths, ordinary and
legacy/NT shared textures, source updates and both admitted blend modes. The
[v31 guest run](../../../docs/validation/2026-10-09/registered-draw-v31/report.json)
passes 24 registered CPU draws and 38 regression readbacks with clean removal.
The physical NPU separately passes 16 component draws with allocation shims;
these results do not establish registered NPU execution. Requesting NPU in the
CPU-only guest fails without fallback or leaked resources. All probe modes need
an explicit `NR_REGISTERED_RUNTIME` directory holding the pinned OpenVINO DLLs;
the test package separately verifies those dependency hashes.


Both draw executables accept `--desktop-size` after their backend argument. This
runs the same independent pixel oracle at 1920×1080 and 2880×1800. Surface bounds
match the kernel contract (8192 per dimension, at most 128 MiB including padded
rows). Installed tests at these sizes require the separate system-page adapter;
the old 16 MiB segment cannot hold even one native-panel image. Component tests
continue to use allocation shims and do not establish desktop frame delivery.
The [v34 live validation](../../../docs/validation/2026-10-09/registered-desktop-v34/report.json)
passes 24 registered CPU draws at both sizes plus six clear regressions: 30
readbacks verify 348,438,528 bytes. Delta's CPU and NPU each separately pass 16
native-size component draws. The driver unloads, mapped pages return to zero,
and the original display stays healthy. Registered NPU execution, presentation
and DWM migration are not tested in that v34 run.

The DXGI blit-present entrypoint now forwards the actual source/destination
allocation handles, kernel context and opaque DXGI context to the runtime's
`PresentCb`. It preserves callback status (including occlusion), rejects
unsupported flags and mismatched resources, and invalidates the context after
a failed callback. Statistics ABI 6 distinguishes requests from accepted
callbacks; neither counter means a frame reached a display. The component test
uses explicit callback shims and verifies that forwarding does not secretly
copy pixels. Live swap-chain presentation remains a separate test; flips,
cross-adapter imports and the other DXGI display functions are still unsupported.
The driver retains the callback table address so runtime updates between calls
take effect, as required by the
[DXGI DDI contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/supporting-the-dxgi-ddi).
The component test replaces `PresentCb` after the first call and verifies that
the next invocation reaches its replacement.

Device creation now returns `DXGI_STATUS_NO_REDIRECTION`, as the same DXGI
contract requires for adapters without a shared Direct3D 9 hardware
implementation. This selects CPU-accessible presentation rather than D3D9/DWM
shared-resource redirection; it does not change the explicitly selected draw
provider or establish frame delivery. The component tests require this exact
successful status and continue to exercise resource, draw and present callbacks.
Earlier live packages through v41 returned `S_OK`; their measurements remain
unchanged. The [v42 live test](../../../docs/validation/2026-10-10/registered-window-v42/report.json)
verifies 12 displayed frames: nine primary-color clears across three startup
variants, followed by three patterned CPU shader blends. The explicit-visibility
probe reports a visible, nonminimized, uncloaked window and holds the display
awake. The NPU-only request separately fails without fallback in the CPU-only
guest. All resources are released and the registered driver unloads cleanly.
The [v7 physical NPU component run](../../../docs/validation/2026-10-10/registered-component-v7/report.json)
passes all 16 native-size draws with this status change and test allocation
callbacks. It establishes neither installed-adapter execution nor screen output.

`--present` creates a visible 320×192 HWND and a discard-model DXGI swap chain
on the selected laboratory adapter. It requires an interactive guest session,
clears the back buffer red/green/blue, submits each present, and checks every RGB
byte captured from that window's screen rectangle after DWM flushes. The GDI
capture is only the independent output oracle. An occluded present, pixel
mismatch or rejected callback fails the test. This tests CPU clear presentation;
it does not move DWM or establish NPU execution. Unsupported DXGI callbacks now
emit their table offset to aid diagnosis without claiming implementation.
The probe now keeps the display awake for its lifetime, requires its thread's
desktop to match the input desktop, explicitly shows its owned window even if
the launcher supplied a hidden startup state, and records visibility, minimized
and DWM-cloaked state before testing. Earlier probes only checked the window
station and session; those checks did not prove that the HWND was visible or
the display awake. Existing failed measurements are retained unchanged.

`--present-draw-cpu` and `--present-draw-npu` connect the admitted texture shader
to that same swap-chain screen oracle. Each of three frames blends a different
spatial alpha pattern over an opaque primary color. An independent integer
reference checks every displayed RGB byte, with the existing one-byte blend
tolerance. The selected provider must account for exactly three executions;
fallback and unsupported shaders remain disallowed. CPU uploads and kernel
copies are still explicit. A passing CPU run does not establish NPU execution,
and neither mode migrates DWM. The v42 guest's CPU mode passes three patterned
screen frames with exactly three CPU executions and no cleanup errors. Its NPU
mode still requires a device-backed validation path; the guest has no NPU.
The first live window attempt reaches back-buffer creation with
`PRESENT|RENDER_TARGET`, `DISCARD_ON_PRESENT`, and no primary descriptor; the old
resource filter rejects those flags. The next revision admits this copy-style
back buffer with real VidMm storage, while still rejecting primary scanout,
unrelated miscellaneous flags and CPU-mapped presentation targets. Its component
checks cover both color formats, discard/non-discard buffers and invalid inputs.

`--upload` exercises initial texture data, full and partial `UpdateSubresource`,
and empty updates for ordinary, legacy shared and NT keyed textures in both
formats. Patterned 65×37 images have padded source rows; every readback checks
the entire image, including the pixels outside a partial update. The caller's
source storage is overwritten immediately after the API returns. These tests
still execute CPU uploads and kernel-worker copies, not NPU rendering.

Shared targets cannot be locked directly. Uploads use an owned, CPU-visible
VidMm transfer allocation and a submitted copy; partial updates first copy the
existing target into that allocation. A blocking write lock waits for the final
submitted reader before releasing temporary storage. Full uploads zero padding.
Invalid bounds, subresources, source pitches, locked targets and wrong-device
resources are rejected. `upload_bytes` counts uploaded image bytes separately
from staging-copy traffic and submitted-copy counts. The
[v30 guest validation](../../../docs/validation/2026-10-09/registered-upload-v30/report.json)
passes all 24 upload readbacks and repeats the five sharing tests, for 58 exact
readbacks overall. All process cleanup counters are zero and the driver unloads
cleanly. That v30 package admits no shader or NPU draw. The new draw implementation
above has a separate acceptance gate; the v30 evidence is not reused as proof of it.

The target is built with the native DDI build or independently:

```powershell
python tools/build_registered_lab.py --build-dir build/registered-lab --wdk-include-root PATH_TO_WDK_HEADERS
```

The standalone builder always rebuilds from clean objects, runs the contract test,
and records source and binary hashes. MSBuild reused a previous UMD after an
included header changed in the v27 laboratory build; that image lacked the new
callback diagnostics despite the source snapshot including them. Preserve that
build's evidence, but do not treat its source snapshot as an exact build proof.
The clean v28 diagnostic image shows the shared allocation callback returning
`E_INVALIDARG`; this corrects the earlier inference that UMD descriptor validation
rejected the request before the callback.

The [v28 trace](../../../docs/validation/2026-10-09/registered-shared-v28/report.json)
locates the rejection in Windows' `CreateVidMmAllocations`, after its allocation
opening succeeds. The [Microsoft render-only sample](https://github.com/microsoft/graphics-driver-samples/blob/master/render-only-sample/roskmd/RosKmdAdapter.cpp)
documents that shared allocations cannot be CPU visible in a memory segment.
The next revision clears that flag for shared targets and submits a two-allocation
copy into a lockable staging texture. VidMm tracks the source read and destination
write, and staging Map waits for completion. The copy executes on the kernel CPU
worker; it is not NPU execution. Ordinary unshared readback retains its explicit
blocking CPU copy. `submitted_copies` distinguishes these paths in probe results.
The [v29 guest tests](../../../docs/validation/2026-10-09/registered-shared-v29/report.json)
pass all five final cases: ordinary rendering, same-process legacy/NT sharing,
and separate-process legacy/NT sharing. Their 34 readbacks verify 417,792 bytes
across BGRA8 and RGBA8, with zero rejected callbacks or cleanup errors in every
process. The module unloads and the package is removed cleanly. The registered
path still executes on the CPU worker; no desktop or NPU result is implied.

The host test calls the actual UMD callbacks with owned runtime shims. It checks
allocation failure cleanup, rotated submission buffers, absence of a CPU clear
fallback, lock failure unwind, staging mapping, and rejection of stale command
buffers after a failed submission. It is not evidence of live adapter execution.
Installation is limited to the disposable kernel-debug guest and requires a
separately signed, exact-hash package; this build does not install a driver.

The [first registered run](../../../docs/validation/2026-10-09/registered-clear-v24/report.json)
selected the adapter through DXGI and created its kernel context, but D3D11 device
initialization failed after eight rejected UMD callbacks. The last recorded
callback was render-target binding. No clear reached the kernel. The next revision
retains blend/depth/rasterizer/sampler descriptors and render-target bindings
while continuing to reject draws. The diagnostic probe pins the exact
OS-registered DLL to retain error counters if initialization fails; D3D11 still
selects the adapter normally and receives no software-driver module argument.

The [next run](../../../docs/validation/2026-10-09/registered-clear-v25/report.json)
created the Direct3D device successfully. Sampler bindings made during
initialization were still rejected, causing the first texture creation to fail.
Per-call diagnostics identified the vertex, geometry and pixel sampler bindings.
The following revision retains independent descriptors for all 16 sampler slots
in each admitted shader stage; this still supplies no shader execution.

The [v26 run](../../../docs/validation/2026-10-09/registered-clear-v26/report.json)
passes all six ordinary Direct3D clears and staging readbacks. Every visible byte
matches, all runtime-owned objects are released, and no callback is rejected.
The kernel debugger records the six actual render submissions and completions.
This validates the registered CPU-worker path on the isolated guest. NPU execution,
sharing, shaders, presentation, and DWM selection are separate remaining gates.

### Finite physical-NPU exchange laboratory

`--present-draw-npu-remote` is a separate, explicitly selected laboratory
backend (`NPU_LAB_FILE`). The installed UMD in the owned disposable guest writes
three bounded pixel requests; `tools/run_registered_npu_exchange.ps1` transfers
them over an existing authenticated WinRM session to
`nr_registered_npu_worker.exe` on a physical NPU host. That worker hardcodes the
NPU provider and returns pixels only after verifying its execution device.
No network listener or driver installation is performed by the controller.
The returned pixels follow the existing registered presentation path and are
checked independently against the displayed window.

A fresh random job directory, exclusive client claim, per-request random nonce,
sequence, dimensions, byte counts and device identity prevent accidental stale
response acceptance. Messages are limited to 32 MiB per frame and three requests;
a missing reply fails after 120 seconds. Errors latch without backend fallback.
Malformed, oversized and wrong-device messages have explicit contract tests.
Statistics ABI 6 counts `remote_npu_executions` separately from local NPU, CPU
and GPU executions. `tools/validate_registered_npu_worker.py` checks physical
NPU pixels and malformed requests without claiming adapter integration.

This transport exists to validate the real hardware path while keeping the
experimental kernel driver off the physical machine. It is not a production
transport, performance benchmark, DWM migration, or full desktop offload. CPU
uploads, kernel copies, management transfers and the original display adapter
remain involved. Local NPU configuration continues to fail when no local NPU
is available; remote operation requires the explicit laboratory mode.
