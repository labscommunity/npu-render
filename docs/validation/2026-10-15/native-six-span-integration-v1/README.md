# Six-span rendering integration component

This archive qualifies a guarded worker component. It does not qualify a live
desktop, responsiveness, GPU relief or Tahoma performance.

The vectorized coordinate kernel preserves the exact float32 operation order
while evaluating four helper positions together. It passes the original/custom/
original NPU checks: 96 changing windows, 1,572,864 rendered components and
12,582,912 complete output words, including padding. Six full changing-image
comparisons against the existing NPU renderer also pass. Complete warm draws take
363.8641–375.8402 ms versus 9,120.3175–9,391.5101 ms for the existing renderer.
These are different deterministic premultiplied test images, not six captures of
normal application use. The CPU generates test inputs only; vertex execution,
projection, coverage, sampling and blending execute on NPU.

The provider class pins both shader bytecode arrays, the compiled model SHA-256,
tensor layout, source/target shapes, blend and sampler contracts. It validates
rectangle geometry and constant color after NPU vertex execution and projection.
Altered flags, shader tokens, scissor, model bytes and vertex color are rejected.
The bad model is checked without importing it; the changed vertex is checked on
NPU. A rejected geometry does not produce raster pixels. Subsequent valid requests
remain usable. Seven complete comparisons, comprising the original captured image
and six changing images, match all 3,145,728 components each.

The named-pipe worker invokes this class only for admitted requests. Other work
continues through its existing NPU provider with explicit accounting. Provider
failures propagate to the existing process recovery; no CPU/GPU fallback is
introduced. The selected eight-request replay passes, invokes the specialization
three times with 34 calls each, and has one provider generation with no faults.
The complete stream and live integration are separate qualifications.

A byte-normalization lookup variant remains excluded for performance: the three
captured request replays are exact but take about 408–441 ms warm. All three
captures have the same output hash and retained input-window hash, so they do not
establish changing-image diversity. The first changing-image harness failed to
build because of a local variable name collision; the corrected clean build and
failure log are both retained.

The worker and integration component link the unchanged qualified v45 vertex and
provider-cache libraries. Clean-build receipts preserve their hashes. No compiled
graphs, executables, proprietary libraries or captured private pixels are included.
[Full desktop acceptance plan](../../../research/desktop-offload-plan.md).
