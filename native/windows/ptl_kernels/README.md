# Panther Lake SHAVE kernels

These C sources are the custom NPU kernels the isolated worker executes. Each is
compiled with the pinned vendor toolchain (`tools/build_ptl_kernel.py`) and patched
into a compiler-owned carrier graph (`tools/delta/ptl_twelve_row_carrier.py` for the
twelve-row carrier, `src/npu_render/ptl_carrier.py` for the six-invocation add-one
carrier). Compiled kernels, carrier blobs and the toolchain are never committed.

| Source | Worker route / environment variable | Live blob (`%LOCALAPPDATA%\npu-render\vm`) | Patched blob SHA-256 |
| --- | --- | --- | --- |
| `identity.c` | first hardware-qualified kernel (not a renderer) | `ptl-identity-*` | see 2026-10-15 kernel evidence |
| `four_tap.c` | `NR_RENDER_PTL_FOUR_TAP_BLOB` | `ptl-six-span-general-v1` | `710f6e55…` |
| `solid.c` | `NR_RENDER_PTL_SOLID_BLOB` | `ptl-six-span-solid-v3` | `21360321…` |
| `three_texture.c` | `NR_RENDER_PTL_TEXTURE_BLOB` | `ptl-six-span-three-texture-v1` | `481765dd…` |
| `geometry.c` | `NR_RENDER_PTL_GEOMETRY_BLOB` | `ptl-six-span-geometry-v2` | `ea4432ab…` |
| `masked.c` | `NR_RENDER_PTL_MASKED_BLOB` | `ptl-six-span-masked-v1` | `81104c9e…` |
| `program.c` | `NR_RENDER_PTL_PROGRAM_BLOB` | `ptl-six-span-program-compact-v1` | `92e04a67…` |
| `long_shader.c` | `NR_RENDER_PTL_LONG_BLOB` | `ptl-six-span-long-shader-v2` | `895831be…` |
| `complex_shader.c` | `NR_RENDER_PTL_COMPLEX_BLOB` | `ptl-six-span-complex-shader-v1` | `47f69b58…` |
| `point_rectangle.c` | `NR_RENDER_PTL_RECTANGLE_BLOB` | `ptl-six-span-point-rectangle-v2` | `d651d610…` |
| `resident_clip.c` | `NR_RENDER_PTL_RESIDENT_CLIP_BLOB` | `ptl-six-span-resident-clip-v3` | `a5c39dbb…` |
| `command_batch.c` | ordered rectangle batch component (not live) | `ptl-six-span-command-batch-v5` | component evidence |
| `textured_batch.c` | textured rectangle batch component (not live) | `ptl-six-span-textured-batch-v2` | component evidence |
| `frame.c` | frame batch renderer (`NR_RENDER_PTL_FRAME_BLOB`) | see `docs/research/frame-batch-renderer.md` | pending |

The full hashes are pinned by `tools/delta/qualify_ptl_worker.py` and the live
coordinator. The first ten entries use the six-invocation add-one carrier
(`ptl-add1-float16-*`); the batch kernels use the twelve-row Softmax carrier
(`ptl-large-span-softmax-v3/twelve-132096/carrier.blob`, SHA-256 `e549e569…`).
