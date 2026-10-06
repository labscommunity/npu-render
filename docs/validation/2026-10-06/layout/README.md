# Pixel grouping and exact downscaling

These measurements keep the same Panther Lake machine, installed drivers and
OpenVINO 2026.1.0 used by the earlier experiments. They improve existing graphics
operations without changing their pixel contracts or selecting another device.

## Shared BGRA buffers

The Windows NPU graph now groups four consecutive pixels into 16 channels before
source-over. The alpha gather repeats each pixel's alpha for its four components.
Reshape and transpose restore the original BGRA byte order. D3D12 allocations,
NT-handle imports, synchronization and external buffer shapes remain the same.
Widths not divisible by the requested group use the original layout. The default
is four pixels; the probe can compare groups 1, 4, 16 and 32.

| Shared-buffer NPU graph | Original layout, warm calls | Four-pixel groups, warm calls |
|---|---:|---:|
| 1280×720 | 10.1–10.4 ms | 4.8–5.1 ms |
| 1920×1080 | 21.9–22.1 ms | 9.3–9.7 ms |

Each row uses eight changing frames and independent pixel checks with actual NPU
execution and shared input/output allocations. All group 1/4/16 runs passed;
maximum unrounded error was about 0.612 byte. The native window's stricter integer
qualification passed within one byte at both resolutions. Sixteen-pixel groups
were similar in these small runs, so the runtime uses four. These are synchronous
NPU calls, not full presentation. The deliberately expensive fixture/upload/
readback/check time is also preserved in each report.

A portable CPU test covers 24 changing fixtures across group sizes, per-pixel alpha,
and widths that cannot use grouping. Hardware qualification additionally tests the
actual Windows GPU shader and NPU graph at the full requested extent.

An interactive Session-1 control run at 1080p passed GPU → NPU → CPU → GPU
selection and retained NPU after an invalid saved selection. Across 485 NPU frames,
complete processing had a 13.93 ms median and 16.59 ms p99; GPU and CPU medians
were 6.86 and 10.34 ms. The entire run had 1,131 submitted frames, 14 skipped slots
and 42 processing deadline misses across all modes. Every presentation was
occluded by the screen saver. These are control/processing results, not visible
frames or a sustained 60 Hz claim. `control-summary.json` and the compressed raw
report preserve the per-backend counts and renderer hash.

Other mathematically valid layouts were rejected. One-hot convolution packing and
the flat convolution variant produced errors up to 255 byte values on this NPU
stack, despite passing the small CPU oracle. Convolution unpacking was correct but
slower. Flat nearest-alpha and broadcast-alpha layouts passed but did not improve
the original layout. These remain research code, outside the runtime. No particular
compiler defect is assumed to explain the failures.

## Exact 2× downscaling

For factor-two reduction, half-pixel bilinear coordinates are
`(2*x + 0.5, 2*y + 0.5)`. Four neighboring texels have weight one quarter, so a 2×2
average pool with stride two is the same operation, including at the image edges.
The provider uses this specialization only when both dimensions match exactly.
Other ratios still use the original interpolation graph.

The complete 1280×720 → 640×360 NPU resize/blend API improved from the earlier
18.9–25.1 ms to **4.7–5.3 ms**, including host packing, inference, conversion and
damage commit. Preparation fell from about 12.6 seconds to 0.44 seconds in these
captures. GPU and CPU complete calls were around 3.8–4.3 ms. The graph-only probe
also passed changing alpha and opacity against an independent reference.

The native test covers 97 cases on each of CPU/GPU/NPU, including downscale corners,
stride padding, opaque formats, source/source-over, opacity, damage gaps, aliasing
and backend retention. Maximum observed byte error was one. Weston CI uses a real
`weston-simple-damage --scale=2` client and requires reported source dimensions to
be twice the resized extent. Physical Linux NPU display validation remains open.

The NPU compiler emitted a `Debatcher Pass failed` diagnostic during pooling
preparation, then returned a compiled model reporting NPU execution. Qualification
and inference passed. The original diagnostic is preserved; no CPU/GPU fallback
was selected. Other driver/compiler versions still require qualification.

## Reproduction and scope

```sh
uv run --group native python tools/build_native.py --devices GPU NPU CPU
build/native/nr_resize_bench NPU 1280 720 640 360 256 8
uv run python tools/validate_weston.py --downscaled
```

On Windows, compare `nr_d3d12_interop.exe --packed-u8 --width 1920 --height 1080
--pixel-group 1` with `--pixel-group 4`. The native window uses group four
automatically. Its reports record the effective group and whether the run only
performed qualification. Session-0 qualification reports have no submitted frames
and do not establish presentation.

`manifest.json` hashes the decompressed originals retained alongside parsed JSON.
Compiler diagnostics preceding JSON remain in those originals. `context.json`
records the implementation hashes. No experiment here establishes improved Tahoma
inference, total energy savings, or whole-desktop migration.

## Runtime follow-up

Intel publishes [OpenVINO 2026.3.1](https://github.com/openvinotoolkit/openvino/releases/tag/2026.3.1)
and an [NPU driver supporting that runtime](https://www.intel.com/content/www/us/en/download/794734/intel-npu-driver-windows.html).
These experiments have not changed Delta's driver or the project's runtime pin.
A separate compatibility comparison is needed before attributing observed compiler
limitations to current hardware capabilities.
