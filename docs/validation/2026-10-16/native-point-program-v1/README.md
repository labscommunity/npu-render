# Point sampling in the reusable NPU program kernel

The program kernel now accepts point sampling as well as linear sampling, with
independent filter selection for its two sample instructions. Structural shader
admission, clamp addressing, bounded geometry, interpolation and blending retain
their existing contracts. The host transports the texels selected by the NPU;
coverage, addressing, shading, blending and quantization execute on the NPU.

All 424 independent CPU fixtures and 424 unique NPU fixtures pass, including 384
valid and 40 invalid payloads. Seventy-one six-span NPU invocations include two
padding repeats. Eight original-carrier checks pass before and after the modified
kernel. The compiled text occupies 17,344 of the qualified 18,256 bytes. The model
metadata and compiler-owned span limits remain unchanged.

Inspection of all 605 structurally eligible recorded draws admits 468, up from
381. It preserves every previous admission and adds 87 draws from two shader
groups. Existing rectangle routing remains excluded: the earlier failed attempt
to replace it with generic triangles is not reintroduced by this extension.

All 87 newly admitted captures pass four complete image variants each: recorded
pixels, transparent textures/background, half-alpha content and randomized
premultiplied content. All 348 variants match both the existing NPU renderer and
the independent CPU shader interpreter with NPU geometry and blending. There are
no byte tolerances or corrected expected pixels. All 87 recorded images retain
their qualified worker hashes. Each case uses one shared program import.
The independent CPU interpreter is confined to the test executable.

Across the 261 warm pairs, summed complete-draw time falls from 13,586.992 ms to
4,091.211 ms; no pair is slower. Pair order alternates, and the first execution of
each process is excluded from that sum. These are component comparisons with
different test images, not desktop frame-time or Tahoma results. Worker replay,
registered graphics and live desktop integration are separate gates.

Model SHA-256:
`0902c8addc03d08e008582394060e80978ed34082bb82b2687fcdff381eef216`.
Comparison executable SHA-256:
`b12d41a7509969ca221d7075337166d5bec83109887c2d1a03c9f2c04f56462e`.
The archive pins 223 source/evidence artifacts. Private pixel data, device models
and compiled binaries are excluded. `summary.json` derives aggregate counts and
timing sums from the retained full-comparison report.
