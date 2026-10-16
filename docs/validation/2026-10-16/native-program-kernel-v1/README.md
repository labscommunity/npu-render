# Bounded pixel-program kernel on Panther Lake NPU

Kernel source v2 passes qualification v3 on Delta's NPU. All 228 independent
full-span fixtures match exactly: 96 geometry/address-plan cases, 96
filter/program/merge cases and 36 malformed-input cases. The original carrier
passes eight checks before and eight after the custom model. No desktop or driver
state is changed by this component qualification.

The kernel accepts up to eight MOV, MUL, DIV and linear-clamp SAMPLE instructions,
four temporary vectors, three interpolated input vectors, four immediate vectors
and two texture samples. It preserves source swizzles, per-channel write masks,
ordered arithmetic, target format, source/source-over blending and ties-to-even
packing. All coverage, interpolation, sampling address arithmetic, filtering,
shader arithmetic and output packing execute on the NPU. The future host adapter
transports addressed texture words; it does not evaluate pixel shaders.

Each compiler-owned span is 262,144 bytes. Vertex records start at byte 2,048 and
task records at 200,704. Geometry and shade records are 112 bytes, with at most
2,048 tasks per span, 12,288 across six spans and 2,046 vertices. The compiled text
uses 16,720 of the 18,256 available bytes.

| Artifact | SHA-256 |
| --- | --- |
| Kernel source v2 | `2ba7a43a7f2f02edc1f61d9b0c5029ff402c160a99657d14854b48dc44a9e0ff` |
| Linked ELF | `4526d214a5cf7eb623be81110c1dec866cd535c65165a9c48a431d7376d2b669` |
| Patched carrier | `49894f9e468e3e0374740bceb2f12c8854806da2856f82c3b2ff8c1fd6e88da0` |

The archive preserves two development failures. The first CPU fixture set wrongly
expected rejection when disabling an unused varying; the corrected fixture
instead disables the varying used by a sample. The first target build failed
because the compiler emitted `memset` calls and the target has no C runtime.
Source v2 supplies a bounded byte-store implementation; CPU checks and the real
NPU qualification then pass. Neither failure is counted as successful evidence.

This qualifies a bounded instruction component. It does not yet establish exact
complete desktop draws, performance improvement, point sampling, additional
shader instructions, live integration or Tahoma benefit. No model blobs,
executables or private pixel images are committed.
