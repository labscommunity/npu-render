# Isolating the three-texture color-arithmetic mismatch

The instrumented v2 replay finds the first differing shader results in its third
8,192-fragment batch: 13 fragments differ. All replays reproduce the same 15 final
byte differences and identical complete output hashes. They remain rejected.

The v4 trace exports the original NPU shader's six sample coordinates and sampled
colors. Its final color is bit-identical to the original shader for every inspected
fragment, confirming that this instrumentation preserves the compared result.
The generated coordinates and scalar FP32 filtering match every traced coordinate
and sample. The custom NPU shader also matches the separately compiled CPU source
bit-for-bit. Differences arise in the subsequent color arithmetic.

V5 injects those exact NPU sample values into the independent shader interpreter.
All six tested combinations of fused arithmetic order and reciprocal division
still differ in all four final components for the 13 inspected fragments. This
rules out those whole-program alternatives as a complete explanation; it does not
identify the first divergent instruction. Instruction-level tracing is next.

The v3 host diagnostic fails compilation because the device header uses GNU
attributes and the host includes Windows min/max macros. V4 uses ordinary inline
functions, disables contraction with /fp:strict, and defines NOMINMAX before the
Windows headers. Both the failed build and corrected sources are retained. The
actual NPU kernel is unchanged.

These diagnostics execute extra NPU models and CPU oracles; their timings are
contaminated and cannot measure renderer performance. No desktop migration occurs.
The graphics specification permits some operation-specific numerical variation,
but a one-byte image difference alone does not establish conformance. See
[Microsoft's floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules)
and the [Direct3D 11.3 specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
