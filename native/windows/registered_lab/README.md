# Registered Direct3D submission laboratory

This guest-only UMD connects ordinary D3D11 texture clears to the private fill
command exercised by the [v23 kernel test](../../../docs/validation/2026-10-09/kernel-paging-v23/report.json).
It uses the normal registered-adapter loader ABI, real runtime allocation/context
callbacks, and no process-local KMT replacement exports. It does not execute NPU
work, shaders, presentation or desktop composition. Negotiating feature level
10.0 for this restricted test does not establish conformance.

`nr_registered_clear_probe --exercise` selects exactly one paired adapter by KMT
identity and DXGI LUID, creates a D3D11 device on that adapter, and performs three
clears each in BGRA8 and RGBA8. It checks every returned pixel through staging
readback, verifies the expected UMD was loaded, and checks cleanup counters. Both
the probe and UMD refuse to operate outside the owned `NR-GFX-LAB` guest with the
`NR_WINDOWS` volume. The production UMD's readiness gate remains closed.

The admitted operations are unshared 2D render targets, staging textures, clears,
blocking CPU staging copies, and staging read maps. Resource data uses VidMm
allocations with no private pixel-buffer fallback. Clears submit immediately and
replace all command-buffer pointers with the runtime's returned pointers. Failed
submission invalidates the context's writable command pointers. Unsupported
operations report an error to the runtime. Shared textures, arbitrary usage,
initial uploads, shader execution and display operations remain unsupported.

The target is built with the native DDI build or independently:

```powershell
cmake -S native/windows/registered_lab -B build/registered-lab -A x64 -DNR_WDK_INCLUDE_ROOT=PATH_TO_WDK_HEADERS
cmake --build build/registered-lab --config Release
ctest --test-dir build/registered-lab -C Release --output-on-failure
```

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
