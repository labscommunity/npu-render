# Coverage toward the iGPU switch

The project goal is to move graphics computation to the NPU wherever we can
implement correct semantics and useful integration. This table describes actual
coverage, not just whether an equivalent tensor equation exists.

| Function | Current implementation | Missing integration or proof |
|---|---|---|
| Premultiplied RGBA source-over | Explicit CPU/GPU/NPU provider, preparation checks, live backend changes | GPU-native buffer import/export and fences |
| Animated application surfaces | SDL software window, deterministic replay, CPU conversion | Actual presentation timestamps and broader real application adapters |
| Window composition | Eligible blend mathematics demonstrated on Panther Lake | Native compositor adapter, damage/occlusion, standalone display validation |
| Window transforms and scaling | CPU scene placement in the demo only | Typed operation graphs, sampling/edge conformance, compositor mappings |
| Color conversion and filters | CPU linear-sRGB display conversion | NPU graph implementations and end-to-end comparison |
| Client 2D drawing/text | No interception | Graphics-library/application adapters; glyph and path rasterization |
| Game rendering | Restricted triangle research probe only | Shader/API translation, textures, depth/stencil, blending and synchronization |
| General GPU compute | No interception | Per-API dispatch, memory and program translation |
| Video processing | No implementation | YUV conversion, scaling/effects and application/media pipeline adapters |
| Video codecs | No implementation | Codec-specific algorithms and integration; distinguish dedicated media engines |
| Display scanout/link transmission | Existing display hardware retains this role | NPU-produced pixel buffers must reach that hardware correctly |

Display scanout is a physical peripheral function; transferring pixel computation
does not transfer the monitor connection. Keeping the existing display engine
active is consistent with freeing GPU compute resources for inference. Resource
measurements must separate graphics, copy, compute, media and display behavior.

## Platform progression

Windows currently supports the library and live application harness. It does not
replace DWM's renderer. The first desktop integration target is a restricted
Weston renderer on Linux, preserving its display backend and explicit handling of
unsupported operations. A nested run establishes integration correctness; native
DRM presentation is needed to characterize useful desktop offload.

Client applications may still render on the GPU even when composition moves. The
control therefore needs an integration registry and observable per-operation
coverage. It must never report that all GPU work moved merely because a backend
selection says `NPU`.

## Evidence gates

For every newly supported operation or adapter, capture: independent pixel
correctness, actual execution device, complete resource/copy costs, lifetime and
synchronization behavior, failure recovery, and sustained frame/inference results.
Broaden coverage based on these measurements while preserving a working switch.
