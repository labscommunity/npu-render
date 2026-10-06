# Texture sampling and affine operations

These experiments use Delta's Panther Lake NPU and the pinned OpenVINO 2026.1.0
runtime. They add operation evidence, not physical-display or Tahoma benefit proof.

The generic native sampler uses integer texel indices, four interpolation weights,
and source-over. The selected device fetches texels and evaluates interpolation
and blending. CPU code creates coordinates/weights, packs input, stages output and
commits disjoint damage. It is a host-memory bridge, separate from the Windows
shared-buffer window. GPU/NPU native byte tests both reached maximum error of one
byte value against a double-precision oracle, within the two-byte sampling gate.

| Experiment | Observed result | Implication |
|---|---|---|
| Dynamic indexed fetch, 64×64 source / 1,024 output samples | NPU passed changing addresses; warm calls about 1.5 ms | Device-side indexed texture access is possible |
| Dynamic indexed fetch, 1280×720 source / 1,024 samples | NPU passed; warm calls about 5.7–6.3 ms | Source extent substantially affects cost |
| GridSample, 1280×720 source / 128×128 output | CPU passed; NPU max errors ~0.014 (FP16 inputs) and ~0.146 (FP32 inputs) on normalized colors | Reject this path for the graphics contract; FP32 inputs alone did not fix it |
| Native generic sampler, 640×360 → 1280×720 | NPU passed within one byte; 57 batches, approximately 720–886 ms per complete API call | Correct but unsuitable for a 60 Hz full-frame path |
| Same generic API on GPU / CPU | Roughly 48–50 ms GPU, 53–83 ms CPU | The host bridge also needs substantial optimization |
| Specialized Interpolate plus blend, same source/output sizes | NPU passed; warm calls ~9.6–9.8 ms, maximum unrounded error <0.16 byte | Promising specialization; this graph experiment is not yet a compositor frame measurement |

The large generic sampler initially failed NPU compilation with the driver's
segmented-tiling diagnostic at a 256×256 output batch and 640×360 source. Shape
qualification passed at sides 32, 64 and 128. The implementation caps NPU sampling
batches at 128×128 on the same device; it preserves the complete operation/image.
The shape-sweep report predates that cap, so it records the actual failure.

The Weston adapter's independent Pixman oracle now covers 48 combinations of
fractional translation, up/down scaling, rotation, mirroring, nearest/bilinear
sampling, one rectangular source clip, and opacity. CPU execution passes within
two byte values, with exact preservation outside damage. New source extents compile
on the control thread; rendering explicitly stays on Pixman until ready. Failed
preparation and unsupported cases remain counted. Native Linux NPU presentation is
still unvalidated, and the generic NPU performance result prevents a readiness claim.

The JSON files preserve measured values; `manifest.json` hashes the original
captures (PowerShell UTF-16LE for these experiments). Compiler diagnostics preceding
the shape-sweep JSON were excluded from its parsed representation. The three
`research/*sampling_probe.py` files reproduce the graph experiments; the native
benchmark is built as `nr_sampling_bench`:

```sh
build/native/nr_sampling_bench NPU 640 360 1280 720 256 8
```

The API time includes validation, host packing, all inference dispatches and output
commit. Coordinate generation is measured separately and reused for this static
scale. The larger fixture time also includes CPU updates and independent checking.
Neither measurement is a scanout timestamp or a concurrent-inference benchmark.

Primary operation contracts and integration source:

- [OpenVINO Gather semantics](https://docs.openvino.ai/2026/documentation/openvino-ir-format/operation-sets/operation-specs/movement/gather-8.html)
- [OpenVINO GridSample](https://docs.openvino.ai/2026/api/ie_python_api/_autosummary/openvino.opset11.grid_sample.html)
- [Pixman 0.42.2 source release](https://www.x.org/releases/individual/lib/pixman-0.42.2.tar.gz)
- [Pinned Weston Pixman renderer](https://github.com/wayland-mirror/weston/blob/00201e077f5fd8e754ad6888097da720e17dfa86/libweston/pixman-renderer.c)
