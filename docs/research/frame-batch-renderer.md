# Frame batch renderer

Status, 2026-10-20: design and implementation in progress. Target: 30 verified
animation updates per second at the 1024×768 logical desktop in the existing
finite trial harness. This replaces the per-draw synchronous path for eligible
work; unsupported draws keep the qualified per-draw routes as explicit fallback.

## Why the per-draw path cannot reach 30 Hz

Trial v64 (docs/validation/2026-10-20/native-turbo-render-live-v1) records 1,380
DWM draws. The median draw covers 576 pixels but costs 31 ms at the driver
boundary (mean 73 ms). Each draw reads the whole 3 MB render target, crosses two
named pipes, runs 6 to 71 OpenVINO inference requests (vertex shading, projection,
coverage and shading are separate requests) and returns the whole target. About
30 draws per DWM frame give a 1.35 s frame interval. The cost is per-draw fixed
overhead, not NPU pixel throughput, so no per-route kernel gain can close the gap.

## Architecture

1. **Driver (UMD) records instead of executing.** Draws and clears append to a
   per-device batch with fully resolved state: decoded vertices, constants,
   shaders by hash, resource references by backing identity and version, blend,
   scissor, viewport. Nothing is read from the render target at record time.
2. **Flush points** are the Windows ordering boundaries: Present, Flush,
   staging map or copy from a target with pending work, any CPU write to a
   resource a pending draw reads, destruction, and target changes that cannot
   be ordered inside one batch. A batch may contain draws to several targets in
   order; a draw may sample a target rendered earlier in the same batch.
3. **The driver keeps a host shadow of every private surface** (written only
   through the DDI or by batch results) with a version counter. VidMm storage
   is written only when Windows can observe it: presentable/shared surfaces at
   flush, staging and copy destinations on demand. Shared or opened surfaces
   whose content changes outside the DDI (GDI redirection surfaces, other
   processes' swapchains) are *volatile*: the batch carries only the sampled
   texel windows, read from VidMm at flush time. Sampled windows come from the
   vertex UV extents evaluated on the host for planning only; the NPU still
   performs every vertex, coverage, sampling, shading and blend operation.
4. **Worker** keeps resident copies of private resources keyed by backing
   identity and version. A batch request carries resource descriptors, uploads
   for versions the worker lacks, volatile windows and the ordered commands. The
   reply returns the modified tile pixels per target and the worker generation.
   After a child restart the generation changes and the driver re-sends state;
   the driver remains the source of truth, so recovery needs no checkpoints.
5. **Frame kernel.** One SHAVE kernel renders a 128×128 tile of one target for
   an ordered command list: vertex shading, projection and snapping, coverage,
   perspective or linear interpolation, texture sampling from host-packed
   windows, a bounded SM4 interpreter for the pixel program, Porter-Duff and
   component-coverage blending with write masks, in target byte order. Twelve
   tiles execute per inference request on the twelve-row carrier. A frame that
   touches few tiles costs one request.
6. **Fallback.** Programs the kernel cannot execute (loops, derivatives, typed
   loads, relative addressing, SV_Position reads) run through the existing
   per-draw routes against the resident target inside the batch, in order.

## Kernel contract

The shared core `native/windows/ptl_kernels/frame_core.h` is compiled into the
SHAVE kernel (`frame.c`) and into the host reference (`frame_kernel.hpp`). Both
are the same C code with fused multiply-add disabled, so host fixtures and NPU
outputs are compared exactly. Arithmetic follows the scalar shader reference
(`shader_reference.hpp`), the projection executor's snapping and the output
merger's blend quantization. Span layouts are documented in `frame_core.h`.

## Evidence plan

1. Host self-test: the shared core against `reference_vertex` for the 24
   captured DWM pixel programs and 12 vertex programs with random inputs.
2. NPU fixtures: synthetic tiles rendered by the patched carrier compared to the
   host reference byte for byte, including every blend mode, both formats,
   point and linear sampling and window escapes.
3. Worker replay: the retained 972-request capture replayed through the batch
   path, draw by draw and in consecutive groups, compared to the recorded
   outputs.
4. Registered Direct3D qualification, then the finite desktop trial with the
   30 Hz animation, input distribution and restoration.

A faster component or a passing replay does not establish the desktop result;
only the finite trial's verified animation updates do.
