# Graphics-buffer interoperability experiment

`nr_d3d12_interop` is a Windows-only, opt-in hardware probe. CI compiles it but does
not run it on machines without the target hardware. It creates an Intel D3D12
device, three shared cross-adapter L0 heaps, and placed linear buffers. Two heaps
carry background/foreground pixels; the third receives NPU-composited pixels.
The default format is FP16 planes; `--packed-u8` uses interleaved BGRA8 and performs
conversion, source-over, clamping and byte rounding on the NPU.

The sequence is: CPU fixture → D3D12 upload → GPU copy to shared input heaps →
GPU fence completion → NPU source-over using imported NT handles → synchronous
NPU completion → GPU copy from shared output heap → readback → independent pixel
check. Eight changing frames reuse allocations, checking stale-buffer behavior.
All inference requests and remote tensors die before the D3D12 heaps and handles.

This proves a shared allocation handoff only if it passes on hardware. It does not
prove arbitrary texture import, implicit fence interoperability, zero internal
driver copies, scanout, or application/compositor performance. The fixture still
uses upload and readback copies, and serializes producer/consumer completion on
the host. The default fixture is 64×64; `--width` / `--height` accept extents up to
4096, and `--frames` controls the number of changing fixtures.

The probe uses OpenVINO's documented low-level parameter map (`MEM_TYPE`,
`MEM_HANDLE`, `TENSOR_TYPE`) to avoid depending on device-specific headers omitted
from some wheel distributions. String enum values are defined by the pinned
2026.1.0 public header. Heap flags and placement follow the same contract exercised
by Intel's NPU DX12 remote-tensor tests; buffer barriers and bounded fence waits
are explicit here. The runtime must report NPU execution and every pixel must
match the source-over oracle within the existing exploratory FP16 tolerance.

Build with `tools/build_native.py`, then run `build/native/Release/nr_d3d12_interop.exe`
with the pinned OpenVINO `libs` directory on the process PATH. JSON reports include
failure stage, graphics adapter, actual NPU execution, per-call time and complete
fixture upload/inference/readback/check time. Never treat an unsupported import as
a successful copied fallback.

`--host-tensors` is a diagnostic comparison that explicitly disables shared tensor
binding and reports `input_and_output_shared: false`. It helped isolate a large-shape
accuracy issue from buffer synchronization. Normalizing all input bytes to 0–1 and
rescaling the result produced errors near three byte values at 720p/1080p, with
both shared and ordinary host tensors. Keeping colors in byte units and normalizing
only alpha fixed the captured fixtures. Direct NHWC arithmetic was also measured,
but was slower than conversion through planar arithmetic. The final model lives in
`windows/blend_model.hpp` and is shared by this probe and the native window.

The original byte-unit planar graph passed eight changing frames at 720p and 1080p. Warm NPU
calls were approximately 9.8–10.3 ms and 21.1–22.1 ms respectively. The FP16 planar
1080p graph took approximately 3.9–5.0 ms. These are separate small experiments,
not claims of equal clocks or an isolated conversion cost. The complete fixture
time includes deliberately expensive CPU fixture generation and pixel checking;
it is not an application frame-time benchmark. See the dated validation artifacts.

The current graph groups four neighboring BGRA pixels into 16 channels, performs
the same per-pixel arithmetic, and restores their byte order. Shared-buffer tests
reduced warm NPU calls to about 4.8–5.1 ms at 720p and 9.3–9.7 ms at 1080p, with
the same pixel gate. `nr_d3d12_interop --pixel-group 1` reproduces the original
layout; groups 4/16/32 are diagnostic alternatives. Non-divisible widths use group
one. This selection never changes the execution device. See the
[grouping and downscale evidence](../docs/validation/2026-10-06/layout/README.md).

## Native Windows application

`nr_native_live.exe` binds the same shared allocations to an explicit NPU graph or
a native D3D12 compute shader. CPU mode uses integer source-over. All three paths
use a BGRA8 contract in the supplied encoded color space, and startup checks opaque,
transparent and fractional-alpha fixtures at the exact selected resolution against
the integer oracle. GPU/NPU error must be at most one byte value. Every provider is
prepared before the window opens; switching activates an already qualified provider
between frames. A failed qualification aborts startup explicitly.

The application copies one CPU-generated animated foreground into a shared buffer;
its background is uploaded once. NPU mode performs byte conversion and blending on
the NPU, then D3D12 copies the shared output directly into a flip-model swapchain.
There is no per-frame GPU readback or host output conversion. GPU transfers,
host-waited fences, the OS compositor and display engine remain involved. CPU mode
avoids uploading input surfaces and uploads its composed output. The GPU baseline
uses a compute shader, unlike the earlier Python OpenVINO-GPU baseline.

```powershell
uv run --group native python tools/build_native.py --devices GPU NPU CPU
# Put the pinned OpenVINO libs directory on PATH, or use the packaged run.cmd.
build/native/Release/nr_native_live.exe --seconds 60 --sequence NPU,CPU,GPU --switch-every 15
uv run npu-render select NPU
uv run python tools/validate_native_live.py --executable build/native/Release/nr_native_live.exe
```

Keys 1/2/3 choose GPU/NPU/CPU; Escape exits. By default the native window watches the
same canonical selection file as the CLI every 250 ms. `--config` overrides its
path. `--backend` or `NPU_RENDER_BACKEND` pins file selection. Invalid saved choices
keep the active renderer; indexed names and escaped JSON forms are unsupported in
this small adapter. Width must be a multiple of 64 for the D3D12 copy row pitch.
Reports include every frame's stage timings, backend, presentation HRESULT, missed
deadlines, skipped replay slots, and selection/rejection events. Pacing uses a
process-local high-resolution waitable timer.

The first native window/control run on Delta found an active screen-saver desktop:
all calls returned `DXGI_STATUS_OCCLUDED`. Its rendering/control results are valid,
but it is **not a visible-presentation result**. The validator's explicit
`--allow-occluded` option permits only that narrower check. The native inference
benchmark refuses occluded trials, including an occluded initial idle frame.
`S_OK` itself still does not establish physical scanout timing.

`npu-render session --require-desktop` reads session connection state, window
station, thread/input desktops and monitor count. The contention harness records
and checks these before inference warmup and around every trial, including IDLE.
It never changes desktops, sends input or unlocks Windows. Per-frame presentation
checks remain required; matching desktop names alone do not prove delivery.

When launching a limited interactive task from an elevated SSH session, use a
report directory writable by that desktop user, for example
`%LOCALAPPDATA%\npu-render\checks`. An administrator-writable scratch directory
can make the task exit before writing any report. The installed application uses
the normal per-user directory; no broad permission changes are necessary.

```powershell
uv run python tools/benchmark_native.py --executable build/native/Release/nr_native_live.exe `
  --endpoint http://127.0.0.1:8000/v1 --model YOUR_MODEL --inference-device GPU
```

This rotates five GPU/NPU/CPU/idle rounds with three requests each, preserving
per-request and per-frame measurements. It checks for premature renderer exit,
occlusion and unexpected backend changes. The native trace has one foreground
surface, so do not compare its numbers directly with the earlier two-layer trace.
The Windows CI job also builds a private portable bundle containing the runtime,
notices, launch script and file-hash manifest. It requires installed Intel drivers
and the Microsoft Visual C++ x64 runtime, but no Python installation.
The [installed ee4d711 bundle](../docs/validation/2026-10-06/bundle-ee4d711/README.md)
passed pixel qualification with its bundled runtime and records its loaded driver.

For driver qualification, pass `--expected-npu-driver 1005540` to the native
window or benchmark harness. The window checks its own loaded runtime before NPU
compilation and readiness. Its report includes `openvino_runtime`,
`npu_driver_version` and any property-query error. The harness independently
records its Python runtime stack and requires the child report to match too;
adjacent DLLs can otherwise make their runtimes differ. Use the exact version
spelling emitted by the runtime, not the Windows INF version.

`nr_dxcore_probe.exe` inspects Direct3D adapter attributes and device creation.
It is a diagnostic built with the native tools, not a rendering backend. A listed
adapter or successful device creation does not prove custom shader execution.

Primary references:

- [OpenVINO NPU remote tensor interface](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device/remote-tensor-api-npu-plugin.html)
- [Pinned remote property definitions](https://github.com/openvinotoolkit/openvino/blob/2026.1.0/src/inference/include/openvino/runtime/intel_npu/remote_properties.hpp)
- [Intel's pinned DX12 interoperability tests](https://github.com/openvinotoolkit/openvino/blob/2026.1.0/src/plugins/intel_npu/tests/functional/behavior/remote_tensor_tests/dx12_remote_run.hpp)
- [Microsoft shared heaps and cross-adapter restrictions](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps)
- [Microsoft GPU synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization)
- [Microsoft presentation return values](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-present)
