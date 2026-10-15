# Six compiler-owned NPU rendering spans

The isolated captured desktop draw now runs in 390.7646 and 387.4819 ms warm,
versus 9,163.7457 and 9,272.2125 ms on the existing NPU renderer. Three alternating
comparisons match all 3,145,728 byte components exactly. Calls fall from 1,282 to
34, including vertex execution and projection on NPU. These times include strip
packing, execution and output assembly. Iteration zero includes lazy preparation
and a retained pixel artifact write, so it is excluded from warm comparisons.
This is about 24 times faster for one draw, still insufficient for 30 fps or a
100 ms visible-response target. No live desktop, GPU relief or Tahoma result is
established by this archive.

## Scheduling and ABI qualification

A six-row Softmax graph compiled for three tiles supplies two independent spans
per tile. The pinned carrier is admitted by its full SHA-256 and a structural
contract: six invocation records, six disjoint input/output spans, exact tensor
descriptors and relocations, and no DPU tasks. Only the admitted code range changes.
The remaining graph bytes stay identical. A first offline contract incorrectly
expected a generic NOBITS workspace; the corrected contract checks the documented
VPU CMX workspace section type. Twenty-four positive and rejection checks pass.

An identity kernel preserves 6,291,456 arbitrary raw 16-bit words, including NaN
payloads and distinct plane boundaries. Original Softmax checks before and after
pass. The four-sample kernel then matches 1,572,864 rendered components across 96
changing source windows, plus all output padding (12,582,912 total words).
Independent full-source float32 references determine expected results; source
window packing only copies pixels. Out-of-window sampling is explicitly rejected.
The actual replay independently validates the geometry after NPU vertex execution
and projection; the test caller pins the entire captured request.

The compiler's NPU50XX configuration exposes two SHAVEs per tile; this is a
compiler resource declaration, not a measured frequency or utilization claim.
[Intel compiler source](https://github.com/openvinotoolkit/npu_compiler/blob/20e7912fbc230725e1ca7394762348d4f480aa65/src/vpux_compiler/src/dialect/config/IR/attributes.cpp).

## Other candidates and limits

The serial default/turbo diagnostic produces exact images and passes original
health checks. Both models remain imported during the test, and the timings
strongly depend on order. It does not isolate frequency, power or sustained
performance, and does not justify enabling turbo for the desktop. Intel documents
turbo as a performance hint with power implications for other compute workloads;
that must be checked against the goal of leaving resources for Tahoma.
[OpenVINO NPU documentation](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html).

A byte-normalization lookup kernel also passes the independent suite. Its actual
replay is recorded separately; a correctness pass alone does not qualify it as a
performance improvement. Kernel sources, build reports, graph-contract receipts
and hardware checks are preserved without compiled graphs, proprietary tools or
private pixel payloads. Source snapshots retain their original bytes.

[Acceptance plan](../../../research/desktop-offload-plan.md): responsiveness,
live integration, normal application admission, recovery coverage, matched Tahoma
A/B and the install/on/off switch remain open.
