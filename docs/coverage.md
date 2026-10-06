# Coverage toward the iGPU switch

The project goal is to move graphics computation to the NPU wherever we can
implement correct semantics and useful integration. This table describes actual
coverage, not just whether an equivalent tensor equation exists.

| Function | Current implementation | Missing integration or proof |
|---|---|---|
| Premultiplied RGBA source-over | Explicit CPU/GPU/NPU provider, damage batches, saved switch, native Windows shared-buffer window | Visible native display validation; async fence integration |
| Source copy | Native provider, byte-exact tests on CPU/GPU/NPU | Wider buffer formats and resource layouts |
| Animated application surfaces | SDL window and native D3D12 swapchain; NPU byte conversion and composition | Native window occluded on first test; presentation timestamps and wider adapters |
| Window composition | Restricted Weston adapter with damage/visibility; CPU headless and nested tests | Native Linux NPU execution and standalone display validation |
| Window transforms and scaling | CPU scene placement in the demo only | Typed operation graphs, sampling/edge conformance, compositor mappings |
| Color conversion and filters | CPU linear-sRGB conversion; NPU BGRA8/FP16 packing and rounding in native window | NPU color-space/filter graphs and end-to-end comparison |
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

Windows supports the library, live application harness, and a D3D12/NPU shared-buffer
window with validated pixel semantics and saved-switch recovery. Its first display
test was occluded by the screen saver. It does not replace DWM's renderer. The restricted Weston
adapter now runs in Linux CPU CI, preserving its display backend and explicit
handling of unsupported operations. Headless and nested runs establish integration
correctness; native Linux NPU/DRM presentation is needed to characterize useful
desktop offload.

Client applications may still render on the GPU even when composition moves. The
control therefore needs an integration registry and observable per-operation
coverage. It must never report that all GPU work moved merely because a backend
selection says `NPU`.

## Evidence gates

For every newly supported operation or adapter, capture: independent pixel
correctness, actual execution device, complete resource/copy costs, lifetime and
synchronization behavior, failure recovery, and sustained frame/inference results.
Broaden coverage based on these measurements while preserving a working switch.
