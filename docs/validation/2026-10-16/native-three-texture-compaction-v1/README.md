# Compact NPU-covered fragments before texture shading

The host now gathers fragment bits selected by the NPU's coverage mask before
submitting the custom texture shader, then scatters the NPU-produced colors back
for NPU blending. Coverage, interpolation, texture coordinates, sampling, shader
arithmetic and blending remain NPU operations. This exact shader has no quad
derivatives; other shaders are excluded from the specialization.

All nine retained draws and three changed texture/background fixtures per draw
pass against the independently qualified precision-preserving graph: 36
comparisons, 253,968,384 exact byte components. The nine legacy outputs retain
their original hashes. The precision correction and its qualification are
[recorded separately](../native-three-texture-correction-v1/README.md).

For ticket 172, compaction reduces candidate provider calls from 254 to 186.
This comparison's complete candidate times range from 578 to 3,140 ms across
different sizes and fixtures. These are component measurements, not a matched
before/after compaction benchmark, live frame rates or Tahoma results. The desktop
was unchanged. The successful executable SHA-256 is
`827d2e0eaeb07b5a17568029d00279f686d61eb03e4bc008f5167bcbc513c15f`.

The manifest preserves source, build receipts and complete comparison reports.
Pixels, executables and model blobs remain outside the repository.
