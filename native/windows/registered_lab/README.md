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
staging copies, and staging read/write maps limited to the resource's declared
CPU access. Resource data uses VidMm
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
copy pixels. The indirect desktop also requests pure flips with no destination.
Those now forward shared, runtime-owned PRESENT allocations through the same
callback, retaining DXGI's opaque flip metadata. Combined/extra flags, private
allocations and invalid intervals remain rejected. This adds no physical scanout
engine. Live delivery, cross-adapter imports and the other DXGI functions remain
separate requirements.
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

The [v43 hardware and screen run](../../../docs/validation/2026-10-10/registered-npu-window-v43/report.json)
passes all three remote NPU draws and independently checks 552,960 screen RGB
bytes within one byte. It records zero local CPU/GPU/NPU draw executions, exactly
three remote NPU executions, and zero live resources or cleanup errors. The
earlier withheld-reply attempt times out without fallback or a successful present.


The registered UMD now creates and retains bounded SM4 vertex programs, including
MOV, ADD, MUL, MAD, source negation/absolute value and indexed constant buffers.
The translator also handles bounded IF/ELSE/ENDIF, MOVC, comparisons, signed
integer addition/conversion, dot products, division, reciprocal square root,
and min/max. Branch joins retain values initialized on every path.
The [v53 desktop run](../../../docs/validation/2026-10-10/vertex-admission-v53/report.json)
records DWM accepting its 90-word shader through this path. It then requests a
116-word shader that the installed version rejects. Both streams have separately
executed on the physical NPU; their creation by DWM is not draw execution.

`nr_registered_vertex_test --backend NPU` checks the translator with explicit
execution identity and no backend fallback. The
[v32 qualification](../../../docs/validation/2026-10-10/modifiers-npu-v32/report.json)
includes signed-zero source modifiers, indexed constants through 4,096 rows,
4,096 screen-coordinate vertices, and 8,192 checked output components from the
116-word desktop stream. `nr_registered_vertex_stream_probe --shader <token-stream>
--backend NPU` runs captured-stage checks without embedding system shader binaries
in the repository. These fixtures do not establish general shader conformance.

The [v38 conditional-stage qualification](../../../docs/validation/2026-10-10/conditional-vertex-npu-v38/report.json)
executes the captured 1,053-word desktop lighting shader on the NPU. Four passes
exercise both lighting paths and match all 32,768 scalar-reference components
bit for bit. Authored tests cover nested branches and the precision failures
found while implementing this shader. The failure logs and smaller reproductions
are preserved alongside the passing run. A CPU graph's lighting result differs
from the sequential reference; it is not used as an NPU fallback.

Adding DP2 completes the tested set of nine vertex streams embedded in this
`dwmcore.dll`: [36 NPU runs check 223,232 components exactly](../../../docs/validation/2026-10-10/compositor-vertex-inventory-v39/qualification.json).
This static inventory is not an exhaustive desktop/application shader list.
In the [v56 live run](../../../docs/validation/2026-10-10/vertex-inventory-admission-v56/report.json),
DWM creates eight vertex variants and reaches its first pixel shader.

Vertex and pixel arithmetic share `RegisterExecutor`, with stage-specific
declarations and retained pixel interpolation metadata. The
[v40 NPU check](../../../docs/validation/2026-10-10/pixel-stage-npu-v40/report.json)
executes that captured 18-word pixel program and checks 4,096 output components
exactly. Authored pixel arithmetic and bound shader lifetime also pass. Input
values are supplied to the pixel stage; interpolation and rasterization are
not performed by these tests. In the
[v57 live run](../../../docs/validation/2026-10-10/pixel-arithmetic-admission-v57/report.json),
DWM creates that pixel program and next requests a 37-word texture shader.

The [v45 texture qualification](../../../docs/validation/2026-10-10/texture-sampling-npu-v45/report.json)
executes this captured shader and an authored counterpart on the physical NPU.
It checks 8,192 components across point and bilinear filters and changing images.
Point results match exactly; maximum bilinear error is 1.20e-7 against the
independent scalar reference. The provider accepts decoded float4 texels with
one mip and edge-clamped 2D addressing. Active nonfinite coordinates fail;
skipped branches containing them do not. Mips, other addressing/filter modes,
explicit LOD and texture format decoding are not implemented.

The qualification retains failed compiler reproductions. Native Floor and a
plain float/integer/float round trip produce incorrect indices; bounded
nonnegative indices use truncation plus an integer mask. Separate integer
NaN/infinity status bits avoid a miscompiled combined selection condition.
A runtime half-texel parameter prevents constant scale/offset fusion from
silently reducing coordinate precision to FP16. Sampling arithmetic remains
on the selected device; these workarounds do not introduce CPU evaluation.

OpenVINO 2026.1 requires serialized legacy precision metadata plus the accuracy
hint to preserve the tested FP32 arithmetic. Indexed reads gather from an appended
zero row for out-of-range constants. Bit-preserving MOV/modifier outputs use NPU
integer operations and integer tensors because the floating path canonicalizes
negative zero. Host code decodes inputs and copies output bytes. Modified MOV
from arithmetic temporaries without integer bit views remains rejected.
On the tested NPU compiler, floating Select misselects elements and min/max
returns incorrect values. Conditional values use integer-indexed Gather;
min/max uses comparison and the same selection path, with NaN handling.
Negative addends become explicit Subtract nodes before precision annotation:
the equivalent Add/negative-product graph loses precision after compilation.

Input-layout objects retain their descriptors across runtime-wrapper destruction.
The bounded host decoder handles per-vertex 32-bit float/integer formats,
R16/R16G16/R16G16B16A16 float and RGBA8 UNORM, including offsets, strides and
missing-component defaults. The [half-input qualification](../../../docs/validation/2026-10-10/half-input-v46/report.json)
checks all 65,536 half encodings against an independent arithmetic oracle,
including signed zero, subnormals, infinity and NaN. This is host format
decoding; existing NPU vertex/pixel and composition regressions pass separately. Instancing
and out-of-range vertex fetch remain unsupported. This prepares shader inputs;
it does not perform rasterization.

Generic draw execution still requires rasterization, resource decoding and complete
pipeline integration. The existing exact fullscreen composition path remains the
only supported Draw path. In the v59 live attempt, DWM reaches DrawIndexed and
781 indirect swapchain assignments select our prepared adapter. SetDevice fails;
no frames are received or displayed. The trace also records a NoGPUAccess flag
mismatch while opening synchronization objects. Its relationship to the failed
indexed draws is not established.

The guest-only DrawIndexed diagnostic preserves one read-only snapshot per
process (32 MiB cap), including owned shader tokens, state, geometry, constants,
and packed texture pixels. Its `NPDRAW01` records use the locked x64 WDK ABI for
state and require a terminal `complete` record. Shader binaries and captured
pixels stay in private ignored experiment storage. DrawIndexed still returns
DXGI_ERROR_UNSUPPORTED and never reports an execution or writes its target.
These snapshots are inputs for implementation and replay, not desktop offload.


`CoverageExecutor` computes triangle sample coverage on the selected provider.
The [v48 qualification](../../../docs/validation/2026-10-10/triangle-coverage-npu-v48/report.json)
checks 36,864 C++ NPU results against independent 64-bit edge functions, including
both windings, shared edges, degenerate triangles and large signed coordinates.
Inputs are already projected and quantized to 1/256 pixel units, bounded to
±4,194,303 units. Coordinate validation occurs before inference; it does not
compute coverage on the host. Projection, interpolation and draw integration
remain separate unfinished work.

Native i64 arithmetic truncates in the tested compiler. Coverage uses three
base-1024 digits instead: every intermediate fits i32. For a signed carry,
masking away the ten low bits leaves at most 22 significant bits, so conversion
to f32, power-of-two scaling and conversion back are exact. Separate integer
predicate masks avoid a second miscompilation of compound boolean expressions.
Failed reproductions are retained with the qualification. Edge ownership follows
the [Direct3D rasterization rules](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#3.4).


`ProjectionExecutor` performs perspective division, viewport transformation,
depth mapping and nearest-even 1/256-pixel snapping on the selected device.
The [v50 NPU qualification](../../../docs/validation/2026-10-10/projection-npu-v50/report.json)
matches 1,024 scalar-reference vertices exactly, including signed halfway ties.
Inputs must be finite, within conservative guard-band bounds, depth-clipped, and
have positive bounded w; clipping is not supplied by this component.
Native Round corrupts even whole-pixel coordinates on the tested NPU. Explicit
masked truncation, fraction comparisons and integer tie masks preserve the result.
The CPU reference provider differs at a rounding boundary; its test uses the
Direct3D ideal-coordinate tolerance while retaining the stricter exact scalar
comparison for NPU qualification. No fallback provider is selected implicitly.


The first unsupported draw after installed solid fills is now covered by
`PointTextureDrawExecutor`: a full-target, one-to-one point-sampled quad with
uniform white vertex color and forced opaque alpha. Vertex shading and projection
run on the selected provider; checked shader and geometry admission reduces the
remaining rendering to an exact uint8 texture crop on that same provider. Source
and target may use RGBA or BGRA. Scaling, blending, partial coverage and other
shader forms are rejected. This is an incremental desktop operation, not a
complete compositor.

The bounded stream protocol carries one packed source texture in version 2;
version 1 solid draws remain compatible. Both packet versions preserve identity,
size limits and explicit physical NPU execution. Texture frames are exercised by
`run_registered_indexed_stream.ps1 -SmokeTest -TextureSmokeTest`. The controller
still requires an owned disposable guest, a finite task and the private lab
transport. See the [hardware and failure evidence](../../../docs/validation/2026-10-10/point-texture-npu-v71/report.json).

The [v62 installed attempt](../../../docs/validation/2026-10-10/indexed-dwm-v62/report.json)
completed and uploaded 126 live DWM draws through the physical NPU, including 124
opaque texture copies. It delivered no indirect-display frames. The next draw
was a transparent uniform quad clipped to the bottom 48 pixels of the screen.
Repeated rejection caused compositor retries; the finite request limit then
rejected further draws. The guest-only IDD removal required a disposable-VM
restart, followed by verified device and package removal.

`SolidRectangleExecutor` now intersects rectangle coverage with a bounded scissor
on the selected provider. Its optional source-over path admits premultiplied
colors exactly on the UNORM8 grid. Nibble arithmetic keeps byte products exact
under the tested NPU's floating-point lowering. Arbitrary fractional colors use
the existing bounded merger only with full-target scissor; unsupported partial
scissor draws fail without committing target bytes. Both checked quad index
orders are admitted. Pixel scissor coordinates travel in packet version 3;
versions 1 and 2 remain compatible. The point-copy specialization still requires
full coverage; the linear-texture path below also accepts partial scissor.
The stream caches up to eight uniform programs, retaining compiled programs
across alternating draw states. `-SmokeTest -ScissorSmokeTest` checks three
1024×768 packets against independent byte arithmetic and preserved outside pixels.
The [v73 qualification](../../../docs/validation/2026-10-10/scissor-blend-npu-v73/report.json)
records the exact pixel checks, captured-draw replay, VM packet checks and failed
arithmetic prototypes. It does not establish display frame delivery.


`LinearTextureDrawExecutor` handles the next captured DWM shader: SAMPLE followed
by uniform RGBA color modulation, retaining and modulating sampled alpha. A checked axis-aligned quad
with unit clip w supports fractional bounds, scaling, edge-clamped bilinear
sampling, RGBA/BGRA conversion, partial scissor and optional premultiplied
source-over. Vertex shading, projection, coverage, UV interpolation, filtering,
blending and byte conversion execute on the selected provider. Host code validates
state, decodes texel formats, copies inputs/outputs and counts coverage diagnostics.
It does not rasterize pixels or choose a fallback device.

The sampler processes at most 4096 pixels per request, with the same compiled
model reused for every row group. A direct GridSample prototype produced corrupt
NPU output; the implementation uses the previously qualified bounded Gather and
integer-predicate lowering. Packet version 4 adds a checked linear-filter bit;
versions 1–3 remain compatible. Unsupported shaders, perspective/rotated geometry,
varying vertex color, color outside [0,1] and unsupported filtering still fail without committing
the target. This is an incremental operation, not full desktop offload.

The [v63b installed trial](../../../docs/validation/2026-10-10/indexed-dwm-v63b/report.json)
completed 10 live DWM solid draws on the NPU. With deferred swapchain deletion,
all 20 swapchains released and both devices removed without restarting the VM.
No indirect-display frame arrived. The next captured operation is the scaled,
alpha-blended texture handled by the new path.

The [v76 hardware qualification](../../../docs/validation/2026-10-10/linear-texture-npu-v76/report.json)
checks 12,845,056 components against an independent double-precision sampler and
blend oracle. Maximum error is 0.5164 byte units (the limit is 0.6); outside pixels
remain exact. Three replays of the captured transparent-source draw are exact.
The actual DDI shim also verifies changing nonzero scaled/blended textures. One
1024×768 draw currently takes about 2.2 seconds: correctness evidence does not
establish interactive rendering, frame delivery, or a Tahoma benefit.

A subsequent specification review found that v76 interpolated UVs from unsnapped
screen positions, while its oracle made the same assumption. Direct3D sections
[3.4.1 and 15.16.2](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm)
require snapped positions for interpolation setup. The corrected renderer uses
the provider-produced 1/256-pixel bounds for both interpolation and coverage.
A narrow fractional-quad regression amplifies the old calculation's sampling
error to 3.79 byte units. The v76 evidence remains intact as evidence of the
initial implementation; its package was not installed. Texture sampling retains
more than the minimum eight fractional bits required by section 7.18.16.1.

The [v77 corrected qualification](../../../docs/validation/2026-10-10/linear-texture-npu-v77/report.json)
passes 12,910,592 component checks on the NPU with maximum error 0.5183 byte units,
including that regression, and three exact captured-draw replays. A separate
separable-matrix sampling prototype is faster but is not integrated or qualified
as a complete rendering path.

The physical indexed workers require `openvino.dll`, `openvino_ir_frontend.dll`
and `openvino_intel_npu_plugin.dll` from the same complete runtime used for
hardware qualification. The older CPU-only guest DLL bundle is insufficient.
Both indexed controllers check these dependencies before guest configuration and
record their hashes and file versions. The [v65 startup failure and regression
check](../../../docs/validation/2026-10-10/indexed-runtime-v65/failure-analysis.json)
preserve the rejected launch; no drivers were installed during that failure.

The [v67 live trial](../../../docs/validation/2026-10-10/received-desktop-v67/report.json)
completed nine DWM draws on the physical NPU and read back three indirect-display
frames. The latest contains the desktop wallpaper and Windows watermark. Five
Present callbacks succeeded. There is no viewer delivery or full desktop claim:
the trial exposed uniform opacity and buffer-identity rotation as further gaps.

Uniform RGBA modulation now runs on the NPU before source-over blending, including
the modulated alpha. The [v80/v81 qualification](../../../docs/validation/2026-10-10/fade-rotation-v81/report.json)
checks 20,054,016 components, both storage formats, color/opacity changes and 64
invalid draws. Maximum error remains 0.5183 byte units; three captured fade-draw
replays are exact. DXGI identity rotation validates the entire list, then rotates
kernel allocation handles in place. Runtime resource handles stay fixed; existing
views and bound targets see the new backing through their retained allocation
objects. These results do not establish interactive desktop performance.

The sole-display v70 trial activated one indirect display and restored the
previous topology, but DWM recovery rejected a vertex-ID shader and then an
11-level texture. [Its evidence](../../../docs/validation/2026-10-10/sole-display-v70/report.json)
records 17 NPU draws and ten received frames, with no full-desktop claim.

The next implementation admits the captured vertex-ID shader's immutable,
fully initialized indexable tables and integer left shifts. Table lookup,
bounds checking and integer arithmetic execute on the selected provider.
Out-of-range table reads fail before target pixels are committed. `Draw` now
assembles triangle lists/strips, including bufferless geometry; generated IDs
remain separate from vertex-buffer addressing offsets, following sections
[8.4 and 8.6 of the Direct3D functional specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
Failures preserve the original call type in the draw capture for replay.

Unshared RGBA/BGRA textures can own a validated mip chain. Initial data, updates,
copy regions, staging maps and single-level render/shader views address the
selected mip. The resource validates every extent before allocation; views retain
the selected storage after the resource object is destroyed. Transfers remain
explicit CPU staging. Multi-level sampling, array textures and shared mip chains
remain unsupported and fail explicitly rather than silently sampling level zero.

The mixed-draw qualification exposed an NPU request-lifetime failure: constructing
an executor, then destroying the prior cached executor before the first inference,
repeatedly produced `ZE_RESULT_ERROR_DEVICE_LOST`. The same new graph passed in
isolation, including three complete draws. Retiring the prior cache entry before
compiling its replacement made the instrumented combined test pass. Direct NPU
caches and the remote worker now use this order; eviction likewise precedes new
model creation. A failed preparation leaves target pixels unchanged. This is a
hardware-qualified runtime workaround, not evidence of a Tahoma speedup.
The [v87 qualification](../../../docs/validation/2026-10-10/vertex-id-mips-v87/report.json)
preserves both failures, the stage isolation, and two passing clean-build NPU runs.

The [v71 sole-display trial](../../../docs/validation/2026-10-10/sole-display-v71/report.json)
reached alpha-only texture creation in DWM, while Explorer and Start rejected
shader-readable buffer storage. Its latest received frame is black. Both custom
devices were removed and the normal desktop recovered; topology restoration itself
returned Win32 status 31. The collector now disables input on any restoration
attempt or terminal helper result, including that failure.

[A8 texture qualification](../../../docs/validation/2026-10-10/alpha-texture-v88/report.json)
adds unshared packed alpha storage, mip updates/copies/maps and single-level SRVs.
It decodes missing RGB channels to zero, matching the independent WARP oracle;
filtering, shader multiplication and blending still execute on the NPU. Two new
filtered/blended draws pass 32,768 component checks within 0.6 byte units. A8
render targets and shared resources remain rejected. Shader-readable buffers can
own ordinary byte storage; typed buffer views are a separate unsupported operation.

The [v72 trial](../../../docs/validation/2026-10-10/sole-display-v72/report.json)
advanced to point-filtered texture draws with blending and clipping. Its captured
render target contains wallpaper and desktop icons, but the last delivered frame
is black. Explorer/Start progressed to creation of an R8_UINT buffer view.

The modulated rectangle executor now supports point filtering with sampled alpha,
and point shaders that explicitly replace sampled alpha with one. Both paths
perform coverage, sampling, modulation, blending and quantization on the selected
provider. The original unblended full-frame point-copy path remains available.
Lab packet ABI 5 identifies the new point path; mixed filter flags are rejected.
[Qualification](../../../docs/validation/2026-10-10/modulated-point-v90/report.json)
checks 40,108,032 components on the NPU within half a byte and replays both failed
DWM draws exactly three times each. Full-frame draw times remain approximately
1.8 seconds; these are correctness results, not interactive-performance claims.

R8_UINT buffer views now validate and retain their element ranges. Resource views
can bind independently to all three shader stages, and a failed binding preserves
the old state. Typed buffers cannot masquerade as textures in the current SAMPLE
path. Buffer-load shader execution remains unsupported and is not counted as
migrated work. Opaque-alpha linear filtering is also still explicitly rejected.

The [v73 trial](../../../docs/validation/2026-10-10/sole-display-v73/report.json)
passed all five NPU packet preflights but delivered black frames. Explorer and
Start reached an `R16G16_UINT` input layout; DWM then reported allocation failure
on a 16×16 texture. The next input decoder extends 8/16-bit signed and unsigned
components to their 32-bit register representation and preserves integer defaults
for absent channels, following
[sections 3.2.3 and 19.1.3.3 of the Direct3D specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
This is host input preparation; it does not move shader work to the CPU.

The [v74 trial](../../../docs/validation/2026-10-10/sole-display-v74/report.json)
passed the former allocation limit and executed 27 DWM draws on the physical NPU.
Its sole indirect display delivered wallpaper, then black frames. The shell
rejected back-face culling; a taskbar draw required nonuniform vertex colors and
a partial scissor. A status-file sharing error also interrupted automatic
cleanup. Recovery preserved the failed trial, removed the renderer, and restarted
only the disposable VM to complete indirect-device removal after an IddCx
watchdog failure. No watchdog setting was changed.

The [culling and interpolation qualification](../../../docs/validation/2026-10-10/culling-gradients-v92/report.json)
adds exact NPU facing/scissor masks and perspective or screen-linear float4
interpolation for an identity pixel shader. General nonuniform pixel programs
still fail closed. ABI 6 carries culling state for the untextured pipeline.
The CPU prepares input formats and conservatively schedules tiles overlapping
the explicit scissor; coverage, interpolation, and output merging execute on
the selected device. Both actual v74 failed draws replay byte-exactly against
an independent scalar oracle on the NPU. The taskbar draw takes 6.3 seconds;
these are correctness results, not interactive desktop performance.

`tools/observe_registered_desktop.ps1` now owns the finite guest observation and
removal sequence. Atomic status replacement and best-effort cleanup reporting
prevent a reader's file lock from skipping removal. The Windows regression test
holds such a lock, confirms cleanup still executes, then checks reporting recovers.
Full desktop offload and a Tahoma performance benefit remain unproven.

The typed-resource path adds SM4 `ftoi`, `imad`, `ishr` and `ld` for the captured
shell lookup shader. Integer buffer views are normalized to R32_UINT transport
elements, with up to sixteen resource and sampler bindings. Packet ABI 8 retains
strict bounds and explicit NPU execution. Integer operations, missing components,
unbound resources and out-of-range loads have independent scalar checks; a
64×64 synthetic triangle using the captured pixel shader matches every output
pixel on the NPU. FTOI currently requires a preserved integer bit view of its
source (inputs, constants and MOV chains); float-only arithmetic temporaries are
rejected. This remains a restricted renderer: those tests do not establish a
usable Windows desktop or a Tahoma improvement. See the
[clean-build qualification](../../../docs/validation/2026-10-10/typed-resources-v101/report.json).

The next instancing path admits per-instance input streams with step rates
including zero, nonzero instance starts, and generated `SV_InstanceID`. Indexed
triangle lists and nonindexed lists/strips expand into the existing NPU triangle
pipeline; host work is input decoding. Captures retain both instance arguments.
The input fetch follows the [D3D11.3 drawing contract](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#8.4).
Immutable, initialized shader-resource textures are admitted while later writes
remain rejected. Textured work bounds use the actual scissor in both the packet
decoder and executor, and the physical worker retains four compiled textured
draw shapes. These changes are laboratory capabilities, pending the next live
desktop trial.

The [v105 qualification](../../../docs/validation/2026-10-10/instanced-desktop-v105/report.json) records the CPU and physical NPU checks and their exact build provenance.

The [v77 trial](../../../docs/validation/2026-10-10/sole-display-v77/report.json)
completed 115 Explorer NPU draws, then exhausted its 128-request allowance while
Explorer repeatedly retried an unsupported read/write staging texture. No DWM
draw or received frame completed. Staging textures now permit READ, WRITE and
READWRITE maps according to creation access; READWRITE sets neither the kernel
ReadOnly nor WriteOnly flag. Copies involving either runtime-owned allocation
use VidMm submission. Callback tests cover private/shared roundtrips, access
denial and exact pixels; these are memory-contract tests, not NPU evidence.
The stream controller defaults to 512 requests within ten minutes, with a strict
4096-request protocol ceiling. The sole-display observation is separately bounded
and always attempts topology restoration and removal.

Nonblocking staging maps now pass `DonotWait` to VidMm and translate temporary
contention to `DXGI_DDI_ERR_WASSTILLDRAWING` without counting it as a fatal
rejection. Tests force contention, verify ownership remains intact, and retry.
The [ResourceMap contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d10ddi_resourcemap)
and [LockCb flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-_d3dddicb_lockflags)
define the runtime and memory-manager statuses separately. Live map arguments are
traced so the next trial can test this explanation of v78's rejection.

Textured triangles now pack the scissor rectangle into fixed-size batches.
Padding lanes sit outside the scissor, and the NPU still computes their coverage.
The CPU only generates addresses and gathers/scatters pixels; it does not test
triangle coverage. This reduces the actual 24×24 DWM capture from 162 to 27
triangle batches, with byte-exact CPU and NPU replay. Partial batches, full
targets, empty regions, culling, both pixel formats and both sampler filters have
independent pixel checks. This is an isolated rendering improvement, not desktop
delivery or a Tahoma performance result.

The [v108 qualification](../../../docs/validation/2026-10-10/adaptive-batches-v108/report.json)
adds scissor packing and device-confirmed empty-tile skipping to untextured
draws. Textured batches shrink when the entire clipped region fits a smaller
batch; cache identity includes that size. Four alternating real/derived DWM
packets return exact NPU pixels. The physical worker also retains at most four
slow non-preflight packets in its owned evidence directory for offline diagnosis;
these captures do not include the transport authentication secret.

The [v84 desktop trial](../../../docs/validation/2026-10-10/sole-display-v84/report.json)
reaches a 24×24 DWM alpha-mask draw using `ZERO, SRC_ALPHA, ADD` for both
color and alpha. The output merger now represents this destination-in operation
explicitly: covered, enabled channels become the previous channel multiplied
by clamped source alpha. Source-copy and premultiplied source-over remain
separate operations. The source-copy rectangle shortcut is disabled for
this mask, including opaque alpha. Clipping, culling, channel masks and UNORM
rounding retain their existing contracts. These factors follow the
[Direct3D blend definitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/ne-d3d10umddi-d3d10_ddi_blend).

Transport ABI 9 carries destination-in for uniform or general textured triangles;
legacy ABI 1–8 packets retain their meaning. Unsupported specialized texture
packets, missing blend enable, mismatched versions and unknown flag bits are
rejected. Blend mode is included in executor cache identity. The offline replay
also applies the new mode in its independent scalar pixel oracle. This remains
a bounded laboratory renderer, with a usable desktop and Tahoma benefit still
requiring live evidence.

The [v120 qualification](../../../docs/validation/2026-10-10/destination-in-v120/report.json)
records byte-exact CPU, scalar and NPU replay of that DWM mask, 18 clean CPU
contract suites, real NPU mask checks and an ABI 9 worker response with all
16,384 output components verified. Live desktop delivery remains the next test.

The [v85 trial](../../../docs/validation/2026-10-10/sole-display-v85/report.json)
captures a point-sampled DWM draw with a 1280×48 target and 1024×768 source.
A one-to-one crop cannot cover that target. Draw selection now uses the existing
affine point sampler whenever either source dimension is smaller, preserving
clipping, clamped coordinates, opaque alpha and modulation on the selected
provider. The driver contract additionally enlarges a 4×4 texture into a 64×64
target and checks every resulting byte.

Broader NPU checks exposed incorrect RGB ordering in the opaque sampler graph
with explicit model caching enabled, including a fresh cache. The earlier graph
passes with that cache disabled; the underlying runtime cause is unresolved.
Sampling now uses separate source and modulation channel inputs, avoiding packed
channel gathers and the intermediate opaque-alpha concatenation. Host work
deinterleaves input storage; texture lookup, modulation, blending and quantization
remain on the selected provider. The scalar texture oracle checks both storage
orders, source-over blending, opacity, clipping, fractional positions and dynamic
input. The [v123 qualification](../../../docs/validation/2026-10-10/point-upscale-v123/report.json)
records all three NPU sampler suites passing both initially and with reused
cache entries, plus byte-exact replay of the captured DWM draw. Live desktop
delivery remains unproven.

The v86 desktop capture adds a 24×24 inverse-alpha mask: `ZERO, INV_SRC_ALPHA,
ADD` for both color and alpha. The explicit destination-out operation multiplies
existing channels by one minus clamped shader alpha. ABI 10 carries this mode
separately from ABI 9 destination-in, with both mask bits together rejected.
Both destination modes bypass source-copy rectangle shortcuts and participate in
executor cache identity. The NPU tests cover varying alpha, clipping, channel
masks, byte rounding, driver binding and the worker packet.

That same draw has vertices approximately 524,312 pixels off-screen. Projection
and coverage now admit a larger guard band. Coverage uses three signed base-1024
limbs per edge difference and five product coefficients; it keeps exact integer
edge signs without relying on the NPU's truncated native i64 operations. Edge
magnitudes fit 29 bits, and intermediate coefficients fit i32. Tests include
opposite extremes, degenerate triangles, both windings and nearly cancelling
long, thin triangles, checked against scalar i64 coverage. This extends x/y
admission; depth clipping and the other pipeline limits remain explicit.

The [v127 qualification](../../../docs/validation/2026-10-10/destination-out-v127/report.json)
records exact CPU, scalar and NPU replay, all 18 clean CPU suites, 61,440 exact
NPU coverage checks and the ABI 10 worker response. The
[v86 live trial](../../../docs/validation/2026-10-10/sole-display-v86/report.json)
preserves the failing draw and black received frames; a usable desktop remains
unproven.

The native relay isolates local client departures from the shared NPU worker
connection. It drains a complete worker response before releasing each FIFO
turn; a closed client pipe, malformed local packet or bad acknowledgement then
affects that client only. Invalid worker responses and provider-stream failures
remain fatal. Consumed provider turns count toward the bounded request budget
even when the client leaves before acknowledging its reply. Departure records
include the transfer phase and explicitly mark the reply unacknowledged.

`tools/check_indexed_client_departures.ps1` exercises six real guest-pipe
lifecycle cases, checking every output byte from a healthy NPU client after
each case. It also keeps one reply unacknowledged while another client completes,
checking that acknowledgement does not retain provider ownership. This test
installs no display drivers and does not establish desktop delivery.

The [v128 qualification](../../../docs/validation/2026-10-10/client-departures-v128/report.json)
preserves the old relay's failure and all six corrected-fixture cases passing
against the physical NPU. The
[v87 desktop trial](../../../docs/validation/2026-10-10/sole-display-v87/report.json)
records two background-only frames followed by black frames and the original
shared-transport failure. Full desktop offload remains unproven.

The desktop's captured `INV_DEST_ALPHA / ONE / ADD` operation extends the
output merger to all thirteen canonical Porter-Duff/plus modes. A shared factor
table defines the provider-independent contract. Color and alpha must use the
same admitted factor pair; other combinations remain rejected. Full blend mode
identity participates in executor caching, and general modes bypass the
source-copy rectangle specialization.

Packet ABI 11 carries the nine additional modes, preserving the earlier packet
versions. Uniform, general textured-triangle and multi-resource packets use the
new modes; rectangle texture specializations that only implement source-over
remain excluded. The tests compare both byte formats, varying alpha, clipping,
coverage and channel masks against a separate scalar oracle. A live desktop
trial must still establish usable frame delivery and input response.

NPU qualification exposed incorrect output when the graph included redundant
zero contributions or multiplication by one. The merger now emits direct graph
forms for these factors. The failing builds and isolated reproducer are retained;
the workaround does not identify the compiler's internal cause.

The [v133 qualification](../../../docs/validation/2026-10-10/porter-duff-v133/report.json)
passes all 18 CPU suites, 638,976 NPU output-merger components, 582,656 uniform-draw
components, 52 driver-callback draws and a 16,384-component ABI 11 worker response.
The exact captured CPU/NPU replay has 396 covered samples and zero-valued output;
the varied component fixtures establish the blend arithmetic beyond that capture.
The worker emits a compiler diagnostic while still returning verified NPU pixels;
its raw log is preserved.

The [v89 installed trial](../../../docs/validation/2026-10-10/sole-display-v89/report.json)
completes 148 live NPU draws but produces three black frames. A captured DWM draw
uses two textures and divides interpolated UV coordinates by W. Zero-filled
inactive raster slots create an invalid lookup and reject the whole draw. The
renderer now copies a device-selected live fragment into those inactive input
slots while retaining the original device coverage mask and output addresses.
The replay decoder also recognizes multiple resources independently of typed loads.

Pixel shaders can now use SM4 `deriv_rtx` and `deriv_rty` outside dynamic branches.
The scheduler packs aligned 2×2 quads and interpolates helper pixels beyond the
primitive and scissor. Provider Gather/Subtract nodes compute the derivatives.
Fully inactive quads clone a live quad; helper pixels never write outside the
image. This follows the reference pattern documented for
[deriv_rtx](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/deriv-rtx--sm4---asm-)
and [deriv_rty](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/deriv-rty--sm4---asm-).
Other derivative variants and derivatives inside branches remain unsupported.

The [v136 qualification](../../../docs/validation/2026-10-10/derivatives-v136/report.json)
passes 19 CPU suites, 128 NPU shader components, 119,168 raster components,
53 DDI draws and a 16,384-component derivative worker response. Tests cover the
captured 200-word DWM shader, nested derivatives, odd image sizes, helper pixels,
partial batches, scissor and channel masks. The formerly rejected dual-texture
capture now matches scalar, CPU and NPU pixels exactly. Valid nonfinite-coordinate
rejection remains tested. This qualifies the renderer for another VM trial;
it does not establish a usable desktop or improved Tahoma throughput.

The [v94 installed trial](../../../docs/validation/2026-10-10/sole-display-v94/report.json)
executes the earlier MIRROR blur draw on the NPU and then reaches a new 988-word
DWM pixel shader. Equality, rounding down, and saturated arithmetic are its next
missing operations. The trial records 339 successful NPU requests, including
13 prewarm packets; 338 have an acknowledged relay receipt. Eight frames arrive
under the collector's sole-indirect-display gate. Desktop icons, partial taskbar,
and wallpaper are visible at different times, but Start/Escape snapshots remain
identical. Both drivers are removed successfully and the normal VM desktop is
restored. This is still partial desktop rendering, with no verified usable input
response or Tahoma improvement.

The [v183 component checks](../../../docs/validation/2026-10-10/compositor-scratch-v183/report.json)
cover the captured shader after adding floating EQ, ROUND_NI, and saturated
floating results. CPU and native NPU each check 22,528 components, including
signed zero, NaN, infinity, subnormals, large integral values, source modifiers,
and zero-alpha color cases. The captured shader's maximum absolute NPU error is
3.94e-6 against a 1e-5 test bound; this is not full shader-model conformance.
Comparisons explicitly correct unordered operands, floor keeps negation in the
integer graph before converting back to float, and saturation deliberately
flushes subnormals as permitted by SM4 min/max. Failed compiler and admission
experiments remain in the [v176–v182 evidence](../../../docs/validation/2026-10-10/compositor-experiments-v176-v182/report.json).

[Clean build v184](../../../docs/validation/2026-10-10/compositor-v184/report.json)
passes all 23 CPU suites and the native NPU regressions, including 58 DDI draws.
The later v94 solid desktop draw replays with exact coverage and zero pixel byte
error on both providers. This build is ready for installed trial v95; full
desktop operation remains unverified.
