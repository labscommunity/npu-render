# Full-source shared-memory access and larger direct rectangles

Six original Gather graphs pass on the NPU, with 64 KiB to 8 MiB source buffers
and 4,096 or 16,384 output pixels. Small sources occupy local CMX memory; the
larger ones occupy DDR memory. The latter include a firmware cache flush/invalidate
task before the executable kernel. The first inspector correctly rejects this
previously unsupported two-record layout; that failed inspection is retained.
The unmodified graphs subsequently pass ordinary inference and exact output checks.

The expanded admission contract checks both task records, complete tensor shapes,
strides, types, source relocation/allocation and disjoint output spans. It preserves
the compiler's cache maintenance and all other bytes outside the executable code
extent. Eighty-four offline checks pass. Intel's pinned loader implementation
establishes that the 32-bit relocation replaces the source address with the
resolved symbol plus addend; the initial 0x200000 field is not the allocation size.
[Intel loader source](https://github.com/openvinotoolkit/npu_compiler_elf/blob/d325f45f2cb405b5fa2ff17a30de9469f1641b73/loader/src/vpux_loader.cpp#L144),
[firmware task types](https://github.com/openvinotoolkit/npu_compiler/blob/20e7912fbc230725e1ca7394762348d4f480aa65/src/vpux_compiler/include/vpux/compiler/NPU40XX/dialect/NPUReg40XX/firmware_headers/details/api/vpu_nnrt_shavert.h#L83).

Our own indexed-read/channel-reversal kernel passes 3,145,728 raw-word comparisons
across those spans. Original/custom/original phases check 9,437,184 components in
total. Custom phases transport seeded arbitrary 16-bit patterns without numeric
FP16 conversion. This is direct NPU access to the full supplied source, including
the 8 MiB allocation, not host-side selection of the required texels.

The combined rectangle kernel then passes 384 changing fixtures and 12,582,912
exact custom output components, including 1920×1080 source textures. Sampling,
affine coordinates, blending, format conversion and coverage all execute on the
NPU. Original graph checks pass before and after every shape. These phases include
full source boundaries and changing transparency, tint, UV bounds and clipping.

Performance remains unqualified. The diagnostic median ranges from 4.38–8.51 ms
for 4,096 output pixels and 27.95–29.88 ms for 16,384. Each request includes source
transfer and output copying; timings exclude fixture preparation and presentation.
Increasing batch size alone is insufficient. No live provider replacement or
Tahoma trial was made. Later vector/compiler and direct-provider comparisons are
separate experiments; this record must not be interpreted as their result.

`raw-manifest.json` pins 76 remotely verified files (709,660 bytes). The source
manifest retains collection and primary-source provenance. Compiled kernels,
carrier graphs and proprietary tools are excluded.
