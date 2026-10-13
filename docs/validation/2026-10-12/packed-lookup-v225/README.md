# Captured Notepad packed lookup shader

The v118 desktop trial rejected a 146-word pixel shader during typing. Its
unsigned right shift (`ushr`) and bitwise OR instructions were unsupported.
The renderer now preserves their 32-bit integer semantics, including the low
five bits of the scalar shift count and zero fill for unsigned shifts.
The scalar reference and OpenVINO implementation are checked against independent
C++ unsigned operations across all shift counts and high-bit inputs.

Both clean builds pass 28 CPU suites. The captured shader passes a clipped
64×64 lookup raster on CPU and on the physical Panther Lake NPU with OpenVINO
2026.4.1 and 2026.1. All four pixel buffers are identical. The raster fixture's
palette repeats after 256 entries, so it does not independently distinguish the
upper two bits of its ten-bit palette index; the separate operation checks use
full 32-bit values. All 71 historical packets and native DDI contracts pass.
The legacy compiler emits a function-outliner diagnostic, retained in its
stderr log; the process exits successfully and the exact output checks pass.

These are captured-shader and operation checks. They do not establish that
Notepad typing works in the installed desktop, usable interaction speed, physical
desktop migration, or improved Tahoma performance. Raw captured bytecode and
pixel buffers remain in private ignored evidence, identified by hash.

Instruction semantics: [Microsoft USHR](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/ushr--sm4---asm-)
and [Microsoft OR](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/or--sm4---asm-).
