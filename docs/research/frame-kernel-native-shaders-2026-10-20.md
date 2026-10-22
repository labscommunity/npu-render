# Frame kernel: from an in-kernel interpreter to native SHAVE shaders (2026-10-20)

Measurements taken on Delta (Panther Lake NPU, driver 5540, OpenVINO 2026.4.1)
with `tools/delta/qualify_frame_kernel.py` on the ScatterElementsUpdate
twelve-row carrier (`vm\frame-carrier-scatter-v1\carrier.blob`, code window
24,496 bytes). Request times include the host tensor copies of 12 rows
(about 5-7 ms per request) and are diagnostic only; they are not display
frame timings.

## Interpreter (frame batch kernel v1)

87 fixtures, all exact on the NPU after the restructure (scanline coverage,
incremental weights, pre-decoded instructions): per 12-fixture group
160 / 2520 / 384 / 94 / 2140 / 666 / 114 / 8 ms. Per-stage experiments on
single quads (13,566 pixels): blend about 0.9 us per pixel, each interpreted
instruction 0.4-1.2 us, sampling 0.6-1.2 us. Memory probes showed DDR and
stack loads both cost about 12 ns, so the interpreter's branches and calls,
not memory, dominated. Evidence: `vm\frame-kernel-qualify-v6-v1-Os-082544`,
`vm\frame-native-exp*`, `vm\memprobe-qualify-*`.

## Native shaders

`frame_kernel_test --emit` compiles each captured DWM shader pair to C over a
four-lane vector API (`native/windows/ptl_kernels/frame_programs.h`): vertex
stages as functions, pixel stages as per-program row shaders that interpolate,
shade and blend one scanline without calls except texture sampling.

Toolchain findings: no 64-bit division runtime (`__divdi3`), `switch`
dispatch miscompiled for case 0 (replaced by if-trees), and moviCompile
schedules `CMU.CMB` one cycle too early after `VAU.MUL.f32`; the assembler
rejects it and `build_ptl_kernel.py --fix-hazards` inserts the NOP it asks for.

Blob partition (`frame::blob_ranges`, pixel-stage instruction counts) and
results, all exact against the host reference (`vm\frame-blob<k>-qualify-092755`):

| blob | programs | text bytes | fixtures | group times (ms) |
| --- | --- | --- | --- | --- |
| 0 (<=2) | 10 | 23,008 | 43 | 21 / 14 / 16 / 5 |
| 1 (3-12) | 6 | 22,352 | 27 | 29 / 38 / 6 |
| 2 (13-64) | 2 | 20,464 | 11 | 93 |
| 3 (c71638aa, 111) | 1 | 21,984 | 7 | 73 |
| 4 (39799859, 118) | 1 | 22,160 | 7 | 77 |
| 5 (ec1e65f6, 181) | 1 | 21,920 | 7 | 132 |

A 13,566-pixel quad now costs roughly 3-5 ms on one engine for light shaders
(about 0.2-0.4 us per pixel including sampling) and 33-60 ms for the three
heaviest DWM shaders (about 20 ns per shader instruction). Three captured pairs
stay ineligible (loops or derivatives) and keep the per-draw fallback.

## Carrier v2: a 48,048-byte code window (2026-10-22)

The code window is the `shave.text` section of the ScatterElementsUpdate blob,
and the section can be grown. `tools/delta/grow_frame_carrier.py` derives
`vm\frame-carrier-scatter-v2\carrier.blob` from the pinned v1 blob: the text
section grows from 24,624 to 48,176 bytes, every later section's offset shifts,
the twelve range-descriptor rows advertise 48,048 bytes of code, and the
OpenVINO NPU metadata trailer is re-attached with the new ELF size (the plugin
reads it from the end of the blob and refuses a blob whose trailer no longer
lines up). Bundle v22's blob 17 (the blur, 28,912 bytes of text) passed all
nine fixtures bit-exact on the NPU through the grown window on 2026-10-22; the
inventory then repartitions into five blobs (`frame::blob_ranges`), which the
bundle v23 section of the live-trials report records. Both carriers stay pinned
in `tools/delta/ptl_frame_carrier.py`.
