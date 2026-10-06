# Graphics-buffer interoperability experiment

`nr_d3d12_interop` is a Windows-only, opt-in hardware probe. CI compiles it but does
not run it on machines without the target hardware. It creates an Intel D3D12
device, three shared cross-adapter L0 heaps, and placed linear buffers. Two heaps
carry background/foreground FP16 planes; the third receives NPU-composited pixels.

The sequence is: CPU fixture → D3D12 upload → GPU copy to shared input heaps →
GPU fence completion → NPU source-over using imported NT handles → synchronous
NPU completion → GPU copy from shared output heap → readback → independent pixel
check. Eight changing frames reuse allocations, checking stale-buffer behavior.
All inference requests and remote tensors die before the D3D12 heaps and handles.

This proves a shared allocation handoff only if it passes on hardware. It does not
prove arbitrary texture import, implicit fence interoperability, zero internal
driver copies, scanout, or application/compositor performance. The fixture still
uses upload and readback copies, and serializes producer/consumer completion on
the host. The shape is intentionally bounded at 64×64 RGBA FP16.

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

Primary references:

- [OpenVINO NPU remote tensor interface](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device/remote-tensor-api-npu-plugin.html)
- [Pinned remote property definitions](https://github.com/openvinotoolkit/openvino/blob/2026.1.0/src/inference/include/openvino/runtime/intel_npu/remote_properties.hpp)
- [Intel's pinned DX12 interoperability tests](https://github.com/openvinotoolkit/openvino/blob/2026.1.0/src/plugins/intel_npu/tests/functional/behavior/remote_tensor_tests/dx12_remote_run.hpp)
- [Microsoft shared heaps and cross-adapter restrictions](https://learn.microsoft.com/en-us/windows/win32/direct3d12/shared-heaps)
- [Microsoft GPU synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization)
