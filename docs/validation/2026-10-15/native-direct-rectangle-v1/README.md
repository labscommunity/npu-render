# Direct texture lookup and combined rectangle rendering

Our custom Panther Lake kernel passes full-source indexed lookup and channel
reversal with 256- and 4,096-texel sources. Original/custom/original phases check
98,304 components, including 32,768 from the custom implementation. The layout
validator checks the entire source, index and output spans before modifying only
the graph's executable code extent. Thirty offline rejection/admission cases pass.

A second custom kernel combines affine coordinate generation, point or bilinear
clamp sampling, uniform color modulation, optional opaque alpha, source-over or
copy blending, RGBA/BGRA conversion, rectangle/scissor coverage and channel write
masks. All of that work executes on the NPU. The host packs inputs, submits the
request, and checks outputs against an independent vector reference.

All 256 changing fixtures pass exactly: 65,536 output components across textures
up to 64×64 and 64 output pixels per request. Original carrier health checks pass
both before and after. The raw packed input travels in FP16 tensor storage; the
kernel interprets the bytes directly and returns exactly representable byte
values in FP16 output storage. This is not FP16 arithmetic over the packed input.

Request-through-output-copy median is 0.2868 ms and p95 is 0.42575 ms in this
diagnostic. Those timings exclude packing, reference calculation, graph import
and presentation. They are not a matched comparison with the existing provider
or a desktop frame-rate result. The 64-pixel batch is too small to establish
useful full-desktop performance. Larger spans/batches, provider compatibility and
integration remain required; no Tahoma test was performed.

The 33 raw files (462,468 bytes) match remote SHA-256 receipts. Sources and
diagnostics are retained; compiled graphs, kernels, and proprietary toolchain
binaries are excluded. [report.json](report.json) records the qualified component
and the unqualified integration/performance gates explicitly.
