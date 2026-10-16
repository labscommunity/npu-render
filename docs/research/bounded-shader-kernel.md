# Reusable bounded shader kernel

Updated 2026-10-16. The [six-span kernel qualification](../validation/2026-10-16/native-program-kernel-v1/README.md)
passes 228 independent NPU cases: 192 valid geometry/shading fixtures and 36
malformed inputs, with original-carrier checks before and after. The compiled
kernel occupies 16,720 of the 18,256 available code bytes. This establishes the
bounded kernel, not complete draw or desktop correctness.

The structural lowerer accepts MOV, MUL, DIV and SAMPLE with explicit masks,
swizzles, temporaries and finite immediates. It selects by program semantics,
without a shader-hash allowlist. Fourteen captured programs representing 605 draws
lower successfully; 381 draws pass the revised linear-clamp resource, sampler and routing gate,
which preserves the existing NPU rectangle path. Point sampling remains unimplemented in this candidate.

The host adapter preserves NPU vertex/projection, then combines coverage,
interpolation and sample addressing in phase zero. The host transports addressed
packed texel words; phase one performs filtering, shader execution and blending
on the NPU. One shared serialized imported model serves the adapters. The ABI
uses six 262,144-byte spans, three varying vectors, up to two texture samples,
eight instructions, four temporary vectors, four immediate vectors and one
output. Each span accepts 2,048 samples; the draw admits up to 2,046 vertices.

[Complete-draw qualification now passes](../validation/2026-10-16/native-program-comparison-v1/README.md)
all 381 admitted captures and four image variants each. Every original captured
image matches the qualified worker. Four alpha-replication cases also use a
separate FP32 filtering reference: all 856 differing bytes in the first synthetic
capture are explained by the old graph's FP16 reduction. The candidate matches
the independent FP32 arithmetic, with no tolerance. Original failed comparisons
remain retained in the [integration investigation](../validation/2026-10-16/native-program-integration-v1/README.md).

The candidate now batches up to 12,288 samples independently of the old graph's
batch limit. Summed warm component time falls from 193.982 to 84.852 seconds;
114 individual comparisons are slower. This is not a desktop or Tahoma result.
The worker integration retains the existing rectangle path, solid and four-sample
priority, and older NPU paths for unsupported program/resource combinations.
The independent scalar test reference is excluded from that worker.

The integrated worker passes all 972 retained requests and registered Direct3D.
Live v43 executes 309 program draws, then fails on a separate 181-instruction
shader in the existing NPU graph path. Its cleanup passes. Remaining gates
include eliminating that timeout, responsiveness and sustained desktop operation.
More instructions, filtering modes, derivatives and loops require their own
semantic and performance checks. Unsupported programs keep the qualified NPU
graph path. Neither component timing nor parser admission proves desktop speed,
application migration or a Tahoma benefit.
