# Coverage toward the iGPU switch

The project goal is to move graphics computation to the NPU wherever we can
implement correct semantics and useful integration. This table describes actual
coverage, not just whether an equivalent tensor equation exists.

| Function | Current implementation | Missing integration or proof |
|---|---|---|
| Premultiplied RGBA source-over | Explicit CPU/GPU/NPU provider, damage batches, saved switch, native Windows shared-buffer window; active-desktop 720p presentation checks pass | Physical presentation timing; async fence integration |
| Source copy | Native provider, byte-exact tests on CPU/GPU/NPU | Wider buffer formats and resource layouts |
| Animated application surfaces | SDL window and native D3D12 swapchain; NPU byte conversion and composition; native switching and concurrent inference checks pass at 720p | Presentation timestamps and wider adapters; [regression persists after battery charging recovers, with NPU activity and surrounding stages both implicated](validation/2026-10-07/power-retest/README.md) |
| Window composition | Restricted Weston adapter with damage/visibility; CPU headless and nested tests | Native Linux NPU execution and standalone display validation |
| Existing Windows DWM composition | Process/adapter telemetry and a controlled static/moving-window Tahoma benchmark | [Graphics-renderer integration](research/windows-compositor.md); DWM work has not moved to the NPU |
| Window transforms and scaling | Indexed texel sampling plus one-dispatch bilinear resize; Weston affine/nearest/bilinear and one-rectangle source clipping; 72 CPU/Pixman cases | Specialized NPU 720p API ~12 ms, 1080p ~27 ms; generic sampling is too slow full-frame; Linux NPU presentation pending |
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
window with validated pixel semantics and saved-switch recovery. After verified
driver activation, its 720p control run and twenty inference trials passed active
desktop and presentation-status checks. The [native comparison](validation/2026-10-07/README.md)
did not improve Tahoma throughput. It does not replace DWM's renderer. The restricted Weston
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

The [indexed sampling profile](validation/2026-10-06/sampler-profile/README.md)
narrows the general-transform bottleneck to SHAVE Gather. Owned host staging and
lookup conversion reduced overhead, but the full 720p sampling API still takes
612–690 ms on the tested NPU stack. The [custom-kernel track](research/shave-kernels.md)
records source evidence and explicit Panther Lake bring-up gates; no custom kernel
has yet expanded runtime coverage.
