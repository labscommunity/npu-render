# Two-phase masked-texture draw on the NPU

The bounded custom kernel implements the captured two-texture multiply shader
`d8e0b497…`. Phase zero performs triangle coverage, perspective interpolation and
linear-clamp texel addressing. The host transports the exact addressed texels and
background pixels. Phase one performs filtering, both shader multiplies,
source/source-over blending, write masking and byte quantization on the NPU.
No host coverage, filtering or blending replaces those device operations.

The kernel passes 96 valid and 24 malformed full-span fixtures on Delta's NPU,
with original-carrier checks before and after. The independent oracle uses integer
edge equations and explicitly ordered FP32 arithmetic. A local C comparison also
passes all 120 cases; that local result alone does not prove NPU execution.
The compiled kernel occupies 11,632 bytes within the retained 18,256-byte code
capacity. Model SHA-256:
`81104c9ea8760bbeb7cc028ea2a9c81e8605482af219f57c082c737b9662d1e0`.

The host adapter admits only the exact captured pixel shader, two supported
linear-clamp resources, bounded geometry and supported blend modes. Six negative
checks cover a changed model, changed shader, unsupported format, extra resource,
point filtering and mirror addressing. Two simultaneously alive adapters share
one serialized model import. The existing vertex/projection NPU stages and
nonoverlapping triangle ordering remain in use.

All 41 retained draws pass four image comparisons each, including three changed
texture/background cases per draw. All 1,219,510,272 byte components match the
original qualified graph. Original-image baseline hashes also match the retained
worker evidence. Baseline-first and candidate-first order alternate.

Across these 164 comparisons, summed time falls from 90.843 to 33.105 seconds
(63.56%), and provider calls fall from 19,984 to 10,456. No individual candidate
comparison is slower. Candidate times range from 44.886 to 975.828 ms.
The derivation is retained in [comparison-analysis.json](comparison-analysis.json).
These are component fixtures, not desktop frame rates or Tahoma benefit.

The comparison executable SHA-256 is
`c867191bdcbecae17eee7a78dd77a32049021061d33b2b801be70e9c2ffb5f21`.
The manifest preserves source, compiler receipts, independent checks, complete
comparison reports and the preceding read-only geometry metadata investigation.
Executables, model blobs and private pixels are excluded. Worker replay and live
desktop integration remain separate gates.
