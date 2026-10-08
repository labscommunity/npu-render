# Native graphics textures written by the NPU

Delta passes 14 hardware cases in [the qualification report](qualified/supervisor.json).
The NPU writes changing source-over results into graphics-owned BGRA/RGBA textures.
A separate native Direct3D 11 device opens their NT handles, then copies or shader
samples those results. All 16 frames per positive case match an independent integer
oracle within one byte per channel, through 2880×1800. Graphics debug queues report
no errors and the allocation aliases release cleanly.

This establishes native texture interoperability in one process. It does **not**
establish a registered graphics driver, cross-process texture ownership, DWM
selection, desktop work retirement, general shader support, or Tahoma improvement.
The installed desktop renderer remains active. GPU shader draws in this probe are
explicit validation work, not work claimed as NPU execution.

## Qualified resource path

1. Select the exact GPU LUID reported by OpenVINO. Require
   `CrossAdapterRowMajorTextureSupported`.
2. Use `ID3D12CompatibilityDevice::CreateSharedResource` to create a row-major,
   cross-adapter NT texture with D3D11 render-target and shader-resource metadata.
3. Open it through native D3D11. Import and lock its allocation through a KMT device
   on the **GPU** adapter; verify the mapping covers the allocation extent.
4. Create a process-local standard ExistingSysMem raw allocation alias over those
   same pages. Import the alias handle as an NPU remote output tensor.
5. Complete synchronous NPU inference before the GPU copies or samples the texture.
   A blocking staging readback completes GPU consumption before the next NPU write.

There is no explicit CPU output copy. Two CPU fixture uploads remain per frame.
Host waits provide synchronization; this does not establish a native shared
GPU/NPU fence or exclude internal vendor-driver transfers. Input/output alias
lifetime is bounded by the synchronous requests, and cleanup precedes release of
the texture that owns the pages.

The qualified binary SHA-256 is
`d60e64ae52857c58680e95e6f1c797ac1753f330190dac75c0ab688963ae10bc`.
Source, runtime DLL, supervisor and raw-output hashes are in the report. The machine
uses Windows 11 build 26200, Arc B390 driver 32.0.101.8724, NPU driver
32.0.100.5540 and OpenVINO 2026.1. DirectX Graphics Tools was installed successfully
without reboot; its temporary installation task was removed. The retained
[installation state](exploration/graphics-debug-final-state.json) is UTF-16.

## Failed controls and the mapping distinction

The [exploration files](exploration) are observations from intermediate prototypes;
they are not all source-hashed qualifications of the final program. The final
14-case run above is the reproducible result. The archived v11 source captures the
explicit-DLL NT-import diagnostic; its unchanged dependencies came from base
`2b5a1a0f8e4dfc51f19fb04cf973ca4c63f6ea1b`.

- Direct import of the typed texture handle as an NPU output imports but inference
  reports `ZE_RESULT_ERROR_DEVICE_LOST`; a shared-buffer control succeeds.
- Mapping a 64×256 typed resource through the NPU KMT device reports 16,384 bytes
  where the graphics allocation requires 65,536. The final negative case rejects
  this before inference. Mapping through the GPU provides the required extent and
  a raw alias produces correct pixels. This identifies a resource-representation
  boundary; it does not prove a specific vendor-driver defect.
- Native GPU readback of the earlier NPU-owned legacy DDI texture still produces
  incorrect pixels with the debug layer enabled. This new graphics-owned path
  does not repair or requalify that earlier route.
- `OpenSharedResource1` on our explicitly loaded software DLL crashes before the
  corresponding thunk is reached. Added NT-sharing exports and process-local
  WDDM 1.2 metadata do not fix it. The retained v1-stack and v2-built controls exit
  with access violation 3221225477. Stack inspection suggests an unpopulated
  runtime thunk slot; this is an inference, not a documented Windows guarantee.
  Experimental DLL changes were reverted, and the final probe rejects that mode
  before creating a device.

## Validation time and input memory

An intermediate full-resolution check timed out after 12 correct frames. Its
oracle read the OpenVINO input memory byte by byte. `VirtualQuery` reports
`PAGE_READWRITE | PAGE_WRITECOMBINE` (1028) for that memory. The final oracle uses
ordinary cached fixture vectors and an integer source-over equation, then copies
fixtures into the input tensors. The complete 16-frame display-resolution checks
finish in about two seconds, including compilation, fixture generation, transfers,
inference and validation. The access pattern and memory protection implicate the
old CPU checker; both checker arithmetic and fixture storage changed, so this is
not an isolated memory microbenchmark.

These timings are **not display FPS or Tahoma throughput**. No new Tahoma benchmark
was run for this allocation change. The older retained-window regression remains
unresolved by these results.

## Reproduce

Build `nr_d3d11_texture_interop` using the pinned native OpenVINO environment and
the Windows SDK. Install Windows Graphics Tools for the debug qualification. Then:

```powershell
python tools/validate_windows_texture_interop.py `
  --binary-dir build/native/Release --source-dir native `
  --output results/typed-textures --debug
```

The supervisor refuses to overwrite evidence, enforces timeouts, retains raw
stdout/stderr, and checks positive and negative contracts. `--sample` on the native
probe adds actual GPU shader consumption. Unsupported width/alignment and known
unsafe direct/explicit-DLL imports fail explicitly. The NPU-mapping negative case
captures the qualified driver behavior; a future driver change can require a new
expectation and evidence.

References: [shared heaps](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps),
[cross-adapter resource flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_flags),
[compatibility interface](https://github.com/microsoft/DirectX-Headers/blob/main/include/directx/d3d12compatibility.h),
[Microsoft's cross-API resource creation](https://github.com/microsoft/D3D12TranslationLayer/blob/master/src/Resource.cpp),
[allocation usage and coherency](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking).
