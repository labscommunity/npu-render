# Reusable bounded shader kernel

Updated 2026-10-16. The [six-span kernel qualification](../validation/2026-10-16/native-program-kernel-v1/README.md)
passes 228 independent NPU cases: 192 valid geometry/shading fixtures and 36
malformed inputs, with original-carrier checks before and after. The compiled
kernel occupies 16,720 of the 18,256 available code bytes. This establishes the
bounded kernel, not complete draw or desktop correctness.

The structural lowerer accepts MOV, MUL, DIV and SAMPLE with explicit masks,
swizzles, temporaries and finite immediates. It selects by program semantics,
without a shader-hash allowlist. Fourteen captured programs representing 605 draws
lower successfully; 390 draws also pass the initial linear-clamp resource and
sampler gate. Point sampling remains unimplemented in this candidate.

The host adapter preserves NPU vertex/projection, then combines coverage,
interpolation and sample addressing in phase zero. The host transports addressed
packed texel words; phase one performs filtering, shader execution and blending
on the NPU. One shared serialized imported model serves the adapters. The ABI
uses six 262,144-byte spans, three varying vectors, up to two texture samples,
eight instructions, four temporary vectors, four immediate vectors and one
output. Each span accepts 2,048 samples; the draw admits up to 2,046 vertices.

Complete-draw qualification remains open. The first two selected captures pass
four original/changed-image comparisons each. The third matches the recorded
image and two variants but differs by one byte in 856 components of its randomized
variant. The comparison remains failed. Arithmetic tracing must identify which
path meets the independent contract; preserving historical output alone cannot
settle a precision discrepancy. No live worker uses this candidate yet.

Remaining gates are explicit rejection-reason coverage, independent arithmetic
attribution, every admitted capture with changed images, full worker replay,
registered Direct3D, then live interaction, recovery and sustained operation.
More instructions, filtering modes, derivatives and loops require their own
semantic and performance checks. Unsupported programs keep the qualified NPU
graph path. Neither component timing nor parser admission proves desktop speed,
application migration or a Tahoma benefit.
