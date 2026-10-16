# Precision loss in the old color-shader graph

Instruction checkpoints narrow the observed color difference to the end of the
shader. The traced NPU shader still produces exactly the old shader's final
colors. In all 13 inspected differing fragments, replacing only instruction 112's
FP32 coefficient 0.019999999552965164 with its FP16-rounded value
0.0200042724609375 makes the independent interpreter match all four old NPU output
components exactly. Changing instruction 111 instead, or both instructions,
does not match. This is evidence of coefficient precision loss in this particular
compiled expression; it is not a claim about every NPU arithmetic operation.

An independent minimal NPU probe checks 16,384 components per expression. The
negative-input multiply-add differs from its scalar reference in 16,050 components.
Moving the negation to the exact immediate coefficient, or expressing multiplication
followed by subtraction, matches all 16,384 components. The positive-input control
also matches. The CPU oracle verifies that both negative-expression rewrites are
bit-equivalent to the original expression for every fixture. The minimal failing
expression is not fully explained by the single half-coefficient substitution;
its half-coefficient match count is retained rather than generalized away.

The precision report's `passed` flag means the four-case diagnostic completed with
CPU-equivalent rewrites. Per-variant difference counts distinguish the failing
original expression from the two exact rewrites and control.

The custom kernel retains the original FP32 coefficient. It must now be compared
against the precision-preserving equivalent NPU graph, with all original outputs
and intentional corrections retained separately. Historical replay equality does
not establish that the old coefficient arithmetic was correct. No corrected graph
or new texture kernel is yet installed into the live desktop worker.

The investigation is consistent with the operation-specific precision requirements
in Microsoft's [Direct3D floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules)
and [functional specification](https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm).
It does not substitute for a complete graphics conformance test. Models, binaries
and complete private image buffers are excluded.
