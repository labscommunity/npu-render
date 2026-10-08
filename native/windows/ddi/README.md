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
