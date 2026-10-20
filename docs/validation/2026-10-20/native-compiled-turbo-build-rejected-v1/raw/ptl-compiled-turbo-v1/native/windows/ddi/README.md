# Direct3D command laboratory

This DLL executes a restricted Direct3D 11 command stream on an explicitly selected
OpenVINO CPU, GPU or NPU. It is a step toward a Windows graphics renderer. **It does
not replace Windows desktop composition or route ordinary applications to the NPU.**

The client calls `D3D11CreateDevice` with `D3D_DRIVER_TYPE_SOFTWARE` and the explicit
DLL module. The Windows runtime calls our D3D10 user-mode driver interfaces (DDIs).
An admitted `Draw(3, 0)` executes an OpenVINO graph on the selected device. No
backend fallback is used. This exercises the graphics-driver command boundary;
there is no registered WDDM graphics adapter, kernel driver, swapchain, or display
output in this laboratory. Negotiating feature level 10_0 selects the DDI ABI;
it does **not** establish feature-level conformance.

## Build and check

From the repository on Windows with Visual Studio C++ build tools and the Windows
SDK installed:

```powershell
uv sync --frozen --group native
uv run --frozen --group native python tools/build_native.py --ddi
uv run --frozen python tools/validate_windows_ddi.py --binary-dir build/native/Release --devices CPU GPU NPU --output results/ddi-check
```

The build fetches the checksum-pinned WDK NuGet package and extracts headers into
`build/dependencies`. It does not install the WDK, register a driver, change DLL
search policy, or change Windows security settings. CPU positive and rejection
checks run in Windows CI. GPU/NPU validation requires the corresponding hardware
and installed vendor drivers; absence is a failed check, never a CPU substitution.

The private CI bundle includes `nr_ddi_lab.dll`, `nr_ddi_probe.exe`, the runtime,
and third-party notices. To test that exact bundle (Python needed for supervision):

```powershell
python tools/validate_windows_ddi.py --binary-dir C:/path/npu-render --runtime-dir C:/path/npu-render/runtime --devices CPU GPU NPU --output results/ddi-bundle-check
```

The output directory must be new or empty. The runner records binary hashes,
commands, elapsed wall time, exit status, raw probe JSON, stdout and stderr. These
elapsed times include initialization/compilation; they are not frame benchmarks.
For one manual check, add the bundle's `runtime` directory to the current process
PATH and invoke `nr_ddi_probe.exe` with the absolute DLL path, device, and report
path. No existing application launcher is redirected to this DLL.

## Admitted contract

- Single-mip, single-slice RGBA8/BGRA8 UNORM 2D host-backed textures, at most
  2048 by 2048; eight compiled shapes per device. Probe cases use 64/132 by 48.
- Exact compiled shader tokens for the fullscreen triangle and unfiltered texture
  `Load` in `contract.hpp`. This is a recognized shader pair, **not a shader compiler**.
- One full viewport/target, no depth, stencil, MSAA or scissor; source copy or
  premultiplied source-over with a full channel mask. Only source-over is qualified
  by the current draw probe. Unsupported pipeline operations report a device error.
- Initial data, bounded `UpdateSubresource` rectangles, host `CopyResource`, and
  staging read `Map`/`Unmap`. Views/bindings retain pixel storage ownership.
- Draws execute synchronously. The destination is committed after successful
  inference. There are no interprocess surfaces or GPU/NPU fence translations.
- CPU clears remain explicit (`cpu_clears`); they are not NPU execution. The draw
  probe does not use clears. Sampler state is unused by the admitted `Load` shader.

Eight positive draws cover both formats, both shapes, fractional alpha, and a
second draw after a subrectangle texture update. A separate double-precision
source-over oracle checks every output channel within one byte. Negative processes
check an unrecognized shader, a shared resource request, and a draw without the
required pipeline. They must fail before any inference executes.

`host_upload_bytes` counts initial/update copies into texture storage;
`host_copy_bytes` counts `CopyResource`. `inference_input_bytes` and
`inference_output_bytes` count the host-side tensor transfers around inference.
Mapped output reads and internal OpenVINO/driver traffic are not included. There
is no zero-copy claim. These counters describe our explicit copies, not total
system memory traffic.

The software thunks derive from Mesa 25.3.0; see `NOTICE.txt`. Their handle/LUID
and WDDM-version metadata describe a process-local software adapter, **not the
physical NPU or its graphics capabilities**. Allocation sharing, presentation,
and unsupported synchronization thunks return explicit errors.

## Remaining desktop integration

Windows DWM needs a selectable graphics adapter plus compatible shaders, resource
formats, shared allocations, ownership/fences, cross-adapter output, and recovery.
The DLL currently rejects shared textures and presentation. A private file mapping
between two test processes would not establish WDDM resource sharing with DWM.
Likewise, copying an already-composed desktop to this DLL would retain the original
composition work. Neither is an acceptable desktop-offload claim.

The next driver milestone is a real shared-allocation/fence round trip with an
existing graphics adapter and an isolated-display test of DWM selection. It also
requires implementing the actual composition shader/state stream, not assuming
this one admitted shader represents DWM. System driver deployment and full-desktop
recovery remain unimplemented. No Tahoma performance claim follows from these
small offscreen command checks.

See the repository's Windows compositor research and coverage ledger for the
system-wide target and retained GPU/display responsibilities.

## Shared-resource experiment

`nr_ddi_shared_lab.dll` is a separate, explicitly loaded experimental target. With
NPU selected it uses the real NPU adapter/device and runtime allocation callbacks
for legacy shared textures. Two clients can use ordinary `CreateTexture2D`,
`GetSharedHandle` and `OpenSharedResource`; admitted draws in the importing process
read shared pages through NPU remote tensors. The shader contract above remains
restricted. This DLL is not installed as a graphics adapter or included in the
normal Windows bundle.

```powershell
uv run --frozen --group native python tools/build_native.py --ddi
uv run --frozen python tools/validate_windows_ddi_shared.py --binary-dir build/native/Release --source-dir native --output results/ddi-shared
```

The supervisor requires the Intel NPU, records exact sources/binaries/runtime
hashes, and checks 16 changing frames at each of 64×48 BGRA, 320×240 RGBA,
1280×720 BGRA and 2880×1800 BGRA. The creator releases its original textures after
the worker opens them, then reopens the legacy handles. Both processes must finish
with zero live surfaces/aliases and no cleanup failures. An unaligned width must
be rejected before inference. The worker is contained in a job that ends it when
the parent exits. Shared-resource tests are distinct from the original DLL's
CPU/GPU/NPU draw and rejection tests.

The current shared contract is single-mip/slice RGBA8 or BGRA8 UNORM, default usage,
no CPU map flags, width divisible by 32, and dimensions at most 4096. A process
may have one laboratory device. Only NPU configuration enables shared storage;
CPU/GPU configurations retain private storage and reject sharing. Kernel mappings
are section-backed standard allocations, with a bounded, versioned footer for
this driver's format/dimensions. They are not vendor-independent texture layouts.

A process-local NT alias over the mapped pages bridges legacy Direct3D handles
to NPU remote inputs. The input-copy counter is zero for the qualified shared
source-over draws. Output is still copied from staged inference output after
success, preserving the destination on preparation/inference failure. The fixture
producer uploads pixels and the parent copies for validation. Host events and
synchronous inference coordinate ownership; there are no native shared GPU/NPU
fences or total-system zero-copy claims. `NR_DDI_TRACE=1` prints thunk diagnostics.

**Native GPU texture consumption currently fails.** `--gpu-open` on the native
probe reports whether the Arc device opens the handle, without claiming pixels
are correct. The stronger supervisor mode below requires GPU readback of each
changing result and returns failure when pixels disagree:

```powershell
uv run --frozen python tools/validate_windows_ddi_shared.py --binary-dir build/native/Release --source-dir native --output results/ddi-shared-gpu --gpu-readback
```

On Delta the handle opens, but the GPU reads zeros. Writing through that imported
GPU texture also fails the pixel control, while a normal GPU-created texture
passes. Changing only the underlying KMT owner from NPU to GPU does not fix it.
720p/display-size native-GPU controls terminated with a process access violation;
the diagnostic now rejects those sizes before device creation and its supervisor
only repeats the two small fixtures. Same-driver NPU sharing at those large sizes
passes separately.
This narrows the gap to the imported graphics-resource path; it does not identify
an exact vendor-driver defect. It is not valid to use a successful handle open as
proof of DWM-compatible textures. `NR_DDI_GPU_DEBUG=1` explicitly requests the
optional system graphics debug layer; absence fails, without silently disabling it.

The successful same-driver sharing and failed native-GPU consumption are both
retained in the [hardware evidence](../../../docs/validation/2026-10-08/ddi-shared/README.md).
Full desktop offload still needs interoperable graphics allocations, compatible
shaders/state, a selectable render adapter, presentation/fences and recovery.

A separate [graphics-owned NT texture probe](../../../docs/validation/2026-10-08/typed-textures/README.md)
now passes native D3D11 copy and shader-consumption checks through 2880×1800.
The NPU writes a raw alias of the GPU's mapped pages. This is not the legacy
resource route above and does not make this DLL a selectable graphics adapter.
Explicit software-DLL `OpenSharedResource1` still crashes before the relevant
thunk even after adding NT exports; those experimental exports were reverted.
