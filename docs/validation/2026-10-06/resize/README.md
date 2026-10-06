# Specialized resize and composition

The native C ABI now prepares a bilinear half-pixel resize plus opacity and
source-over graph for a fixed source/output extent. One inference processes the
whole resized image; only requested, disjoint damage is committed. It uses the
selected CPU/GPU/NPU device explicitly, including interpolation and global opacity.
CPU code packs byte surfaces, converts the output, and commits damage. The runtime
handles host/device transfers. This is separate from the Windows shared-buffer
application and is not a zero-copy path.

On Delta's Panther Lake NPU, eight changing 640×360 → 1280×720 fixtures took
**11.7–12.4 ms per complete API call**, with one dispatch and maximum error of one
byte against the independent bilinear/source-over reference. The previous generic
indexed path took approximately 720–886 ms for this same full-frame operation.
These are short off-screen measurements; they do not establish sustained 60 Hz
presentation or a Tahoma improvement.

| Path or extent | Complete API time | Interpretation |
|---|---:|---|
| First specialized implementation, NPU 720p output | 111.5–116.8 ms | Direct CPU access to request-owned tensors was expensive |
| Owned host tensors, NPU 720p output | 35.8–37.7 ms | Transfers are explicit runtime work; CPU conversion remained dominant |
| Host tensors and conversion tables, NPU 720p output | 11.7–12.4 ms | Packing about 2.2–2.6 ms, inference about 5.4–5.8 ms, staging/commit about 3.9–4.1 ms |
| Same optimized API, GPU 720p output | 8.2–8.7 ms | GPU is still faster; offload must earn its value through freed resources |
| Same optimized API, CPU 720p output | 9.7–11.9 ms | CPU remains an essential comparison/control |
| NPU 960×540 → 1920×1080 | 26.7–27.2 ms | Exceeds a 60 Hz processing interval before presentation |
| NPU 1280×720 → 640×360 | 18.9–25.1 ms | Downscaling needs its own optimization; smaller output alone does not imply a faster graph |

Every final benchmark passed within one byte. The native correctness test passed
73 cases on each of CPU, GPU and NPU, covering fractional resizing, opacity,
source/source-over, opaque formats, stride padding, separated damage, aliasing,
invalid requests and backend retention. GPU/NPU → CPU switching prepared retained
resize graphs before activation. All conversions still follow the byte-domain
contract; the lookup tables do not introduce a new rounding rule.

The opacity-variant probe ruled out dynamic opacity as the explanation for the
initial slowdown. It records timing and output sums only, not an independent pixel
test. The complete API and native oracle are the correctness evidence. The host
tensor experiment establishes a memory-path improvement without proving a
particular driver cache policy.

## Weston integration

The adapter chooses this specialization for unclipped bilinear, positive
axis-aligned scales with an integer output origin, an exact output extent and
fractions representable by Pixman's seven-bit filter. Damage must fit the resized
virtual image. Other supported transforms retain indexed sampling. Compilation is
queued on the control thread, with counted Pixman handling while preparation is
pending. Switching prepares both kinds of retained graphs before activation.

The local CPU/Pixman oracle passes 72 transformed cases plus the original two
translation cases, with maximum difference of two bytes and exact preservation
outside damage. The run exercised six specialized resize calls. Linux CI also
tests a real viewport client with `validate_weston.py --resized`; its report must
contain resize calls with zero native failures. CPU integration does not prove
native Linux NPU execution or physical display delivery.

## Reproduction and captures

```sh
uv run --group native python tools/build_native.py --devices NPU GPU CPU
build/native/nr_resize_bench NPU 640 360 1280 720 256 8
# Optional per-call stage diagnostics on stderr:
NPU_RENDER_TRACE_RESIZE=1 build/native/nr_resize_bench NPU 640 360 1280 720 256 8
```

Use `native-build/Release/nr_resize_bench.exe` for the equivalent Windows build.
The tile argument remains the context's blend/sampling capacity; specialized
resizing uses one full-extent dispatch. `coordinate_cpu_ms` belongs to the
independent oracle in resize mode and is not used to render. The fixture timing
includes CPU scene updates and the independent full-image check.

Reports without a `host` or `table` suffix are the initial implementation. `host`
adds owned host tensors; `table` is the final implementation. `context.json` records
hardware/runtime and final source hashes. `manifest.json` hashes the decompressed
original captures retained alongside parsed JSON. PowerShell wrapped stderr lines;
the parsed stage reports join those line wraps and omit its error-stream envelope.
Preparation includes compilation and full-extent qualification and is excluded
from per-call timing. No model weights, device caches or machine addresses are
included.
