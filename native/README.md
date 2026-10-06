# Native graphics integration

The C ABI in `include/npu_render.h` accepts CPU-addressable, top-down BGRA8/BGRX8
surfaces and batches disjoint damaged regions into fixed-capacity inference
dispatches. It implements premultiplied source-over and source-copy using an explicit
CPU/GPU/NPU OpenVINO backend. The byte-domain blend preserves the integration's
existing blend color space; it does not silently convert encoded buffers to linear.

Creation and switching compile and qualify the exact static graph. Switching
prepares a candidate while existing calls can retain the previous program. Infer
requests serialize per program. Every region in one call is staged before writing
the destination, so failures cannot leave a partially composited surface. Input and
output buffers can alias. There is no compilation in the compose path.

The current bridge imports host memory, packs FP16 planes, executes, converts to
BGRA8 and copies changed pixels back. It is **not zero-copy DMA-BUF/D3D interop**.
FP16 source-over allows at most one byte of error against the integer oracle; that
is an exploratory gate, not full graphics API conformance. Source-copy is checked
byte-exactly. Bounds, stride, disjointness and output ownership are validated.

```sh
uv sync --frozen --group native
uv run --group native python tools/build_native.py
# On an Intel machine with those devices available:
uv run --group native python tools/build_native.py --devices GPU NPU CPU
```

This builds under `build/native` using the headers and libraries from the pinned
OpenVINO wheel. Windows requires MSVC Build Tools; Linux/macOS require a C++17
compiler. The tests run the requested devices explicitly and fail on unavailable
devices. The initial single-rectangle and later batched-region implementation were
tested on Delta's GPU, NPU and CPU. Cross-platform CI exercises CPU only.

## Restricted Weston adapter

`weston/` supplies a small patch against Weston revision
`00201e077f5fd8e754ad6888097da720e17dfa86` (the 13.0 branch), plus a C integration.
Weston remains responsible for client buffers, scene visibility, damage and output.
Eligible source/source-over operations go through the native provider; unsupported
ones remain explicitly counted Pixman operations. Native failure retries are safe
because the destination was not partially committed.

Eligible inputs are little-endian ARGB8888/XRGB8888 SHM surfaces, global opacity and
disjoint damage. Integer translation retains the direct blend path. Affine scaling,
rotation, mirroring and fractional translation use indexed texture sampling, with
nearest/bilinear filtering and an optional single rectangular source clip. Bilinear
coordinates match Pixman's 7-bit fractions and edge policy. Multiple source clips,
perspective, other formats and solid fills retain counted Pixman handling. The
adapter does not enable GPU DMA-BUF clients or offload arbitrary client rendering.

`nr_prepare_sampling()` compiles/qualifies each source extent on a control thread.
The adapter counts Pixman fallback while preparation is pending; sampling never
compiles in the paint path. Backend switching prepares all retained sample graphs
before activating the new backend. `nr_sample_regions()` receives integer addresses
and weights; texel fetch/interpolation/blending run on the selected device. Coordinate
generation, buffer packing and output staging remain CPU work. All output commits
after successful inference, and invalid addresses/weights preserve the destination.
Sampling has a two-byte exploratory accuracy gate, separate from blend's one byte.

**Generic sampling is currently a correctness path.** The 640×360 → 1280×720 NPU test
took roughly 0.7–0.9 seconds per host API call. This is not suitable for full-frame
60 Hz use. NPU sampling batches are capped at 128×128 to avoid a measured compiler
failure without changing devices. The specialized resize path below avoids those
repeated indexed dispatches for eligible scales.
See [operation evidence and limitations](../docs/validation/2026-10-06/sampling/README.md).

`nr_prepare_resize()` / `nr_resize_regions()` provide a specialized half-pixel,
edge-clamped bilinear resize plus opacity and composition. The virtual resized
image has an integer origin; disjoint damage must fit it and the destination. One
inference processes the resized extent, with atomic damage commit. Extents are
1..4096. Resize and sampling share a limit of 16 prepared graphs and 64 Mi logical
RGBA FP16 tensor pixels; compiler workspace is additional. All retained graphs
must prepare successfully before switching the active device.

Weston selects this path for eligible axis-aligned bilinear scales with exact
Pixman coordinates. Rotation, mirroring, source clips and other affine cases retain
the general sampler. Graph preparation stays off the paint thread. The optimized
720p resize/compose API took 11.7–12.4 ms on Delta's NPU; 1080p took 26.7–27.2 ms.
See [complete API measurements](../docs/validation/2026-10-06/resize/README.md).

Install Linux dependencies (Ubuntu 24.04 example):

```sh
sudo apt-get install meson ninja-build pkg-config libwayland-dev wayland-protocols \
  libpixman-1-dev libinput-dev libevdev-dev libxkbcommon-dev libudev-dev libdrm-dev \
  libcairo2-dev libpango1.0-dev libpng-dev libseat-dev
uv run --group native python tools/build_native.py --pixman
uv run python tools/build_weston.py
uv run python tools/validate_weston.py
uv run python tools/validate_weston.py --transformed
uv run python tools/validate_weston.py --resized
```

The test uses a real Wayland client against a headless compositor and compares the
adapter separately with Pixman's pixel oracle. Headless CI is integration evidence,
not an NPU or physical-display result. Native Linux Panther Lake validation is still
required. The build installs privately under `build/weston-install`, not system paths.

For an opt-in nested session with an installed NPU driver:

```sh
uv run npu-render select NPU
export NPU_RENDER_NATIVE_LIBRARY="$PWD/build/native/libnpu_render_native.so"
uv run python tools/run_weston.py --backend=wayland-backend.so --renderer=pixman
```

The adapter reads the same saved selection as Python and watches it on a control
thread. `npu-render select CPU` / `GPU` / `NPU` prepares a new backend before
activation. Invalid or failed selections preserve the old renderer. An explicit
`NPU_RENDER_BACKEND` environment override pins the backend and disables file
watching, matching the Python precedence rule. The native adapter currently accepts
unindexed device names and canonical version-1 selection JSON only.

Frame logs name actual execution devices; shutdown logs report native calls/pixels,
Pixman fallbacks and native failures. Check these before interpreting an NPU
selection as coverage. A native DRM build can be prepared with `build_weston.py
--drm`, but standalone display and presentation validation remain separate gates.

The upstream Weston files retain their MIT license notices. The checked-in patch
references upstream source; the preparation tool fetches the pinned revision from
the Wayland mirror and refuses to patch another revision.
