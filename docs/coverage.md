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
| Cooperating Windows surfaces | Three original GDI windows hand off DWM → NPU/GPU → DWM, with source retirement and exact screen checks; retained rectangle placement eliminates recurring host pixel uploads | [Retained-buffer evidence](validation/2026-10-08/retained-windows/README.md); GPU copies, full-canvas blending and outer DWM remain; Tahoma still trails Windows |
| Restricted Direct3D draw stream | Process-local D3D11 software-driver DLL executes an exact admitted shader pair on CPU/GPU/NPU; texture updates and explicit rejection checks | [DDI laboratory](../native/windows/ddi/README.md); host copies, no presentation or general shader compiler |
| Shared Direct3D textures on NPU | Separate experimental DLL supports real legacy handles across processes, NPU remote inputs and resource release/reopen | [Shared-resource checks](validation/2026-10-08/ddi-shared/README.md); native GPU accepts the handle but fails the pixel oracle; no DWM-compatible output claim |
| Graphics-owned textures written by NPU | Native GPU draws input textures; separate NPU worker blends; native GPU samples output through 2880×1800; creator release/reopen and process-exit checks pass | [Cross-process hardware checks](validation/2026-10-08/texture-worker/README.md); no explicit CPU pixel uploads, host waits and validation GPU work remain; no DWM driver selection |
| Captured Windows desktop | Per-window capture, NPU composition, opaque click-through output, occlusion spans, hotkey switch and external watchdog | [Capture experiment](../native/windows/capture/README.md); retains GPU/client work; original taskbar/shadow fidelity fails; no Tahoma benefit established |
| Windows shared allocation ownership | Real NPU/GPU KMT standard section allocations, child-process NPU imports, GPU consumption without CPU result upload | [Memory experiment](../native/windows/wddm-memory.md); host waits and diagnostic GPU alias; D3D11 shared-texture callbacks and DWM selection still absent |
| Arbitrary Windows DWM composition | Experimental registered renderer in a disposable VM sends supported live DWM draws to the physical NPU; sole indirect display has received wallpaper | [Latest live trial](validation/2026-10-10/sole-display-v80/report.json) completes 20 Explorer NPU requests but no compositor draws or received frames in a fresh VM. Full desktop functionality, interactive speed, physical scanout, and a Tahoma benefit remain unproven |
| Registered Windows textures and restricted draws | Guest driver accepts D3D11 clears, uploads, sharing, keyed mutexes, submitted copies and an exact composition shader pair | [Registered-driver evidence](validation/2026-10-09/registered-draw-v31/report.json); 24 CPU draws plus 38 regression readbacks. Physical NPU separately passes 16 draws with allocation shims. Registered NPU execution, general shaders, desktop-size backing, cross-adapter output and presentation remain |
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
