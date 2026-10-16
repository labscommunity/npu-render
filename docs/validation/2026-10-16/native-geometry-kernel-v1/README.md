# NPU coverage and interpolation kernel

The new six-span component combines fixed-point triangle coverage and four FP32
varying registers in one NPU call. It supports both windings, top-left edge
ownership, culling, scissor clipping, flat and perspective interpolation. The host
transports already projected vertices, triangle identifiers and sample positions;
coverage and interpolation arithmetic execute on the NPU.

The independent Python oracle uses integer determinants and explicit FP32 steps.
All 96 valid and 12 malformed full-span fixtures pass on Delta's NPU; original
carrier checks also pass before and after. The valid cases include 17,871 covered
samples, shared edges, degenerate triangles and large coordinates. A separate local
compile passes the same fixtures; it is recorded only as a CPU check.

The kernel admits at most 252 projected vertices and 3,072 samples per span.
Six spans carry up to 18,432 samples. Its 7,920-byte code fits the previously
qualified carrier without changing scheduling metadata. The model SHA-256 is
`b848e84cd4faf27a2ec08f5dc6cc7bc6f6099ab51281524623dcb4c8a3ade4c2`.

Integrated with the three-texture shader, it passes all nine recorded draws and
three changed texture/background fixtures each: 36 comparisons and 253,968,384
exact byte components against the qualified precision-preserving graph. Ticket
172 uses 150 calls and roughly 556–590 ms, versus 146 calls and roughly
2,501–2,764 ms for that graph reference in this comparison. The previous custom
geometry implementation used 186 calls in a separate run. Those separate runs are
not a matched comparison isolating this change. Across all sizes and fixtures,
candidate times range from 498 to 2,005 ms. These are complete-draw component
times, not frame rates or Tahoma results.

The largest remaining replay shader is also exported and parsed without pixel
data: two samples followed by two multiplies. The export motivates broader use
of the geometry kernel; it does not qualify that shader's integration. This
component has not yet been promoted into a live desktop worker.

The manifest preserves sources, local checks, NPU qualification, build and full
comparisons. Executables, model blobs and pixels are excluded.
