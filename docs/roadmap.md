# Milestones toward the graphics switch

The end goal is to select an NPU graphics mode once and have participating
integrations move as much eligible work as possible, preserving visual and frame
delivery requirements while freeing useful GPU capacity.

| Milestone | Deliverable | Acceptance evidence | Status |
|---|---|---|---|
| Device feasibility | Deterministic composition and restricted triangle graphs | Independent checks on Panther Lake NPU, including reported execution devices | Original probe complete |
| Shared runtime | Provider contracts, device selection, prepared switch, owned outputs | 43 Python checks, native pixel tests, real device switching | Implemented |
| Live application | Animated software window, async switch, frame-stage timing | Windows console session; 2,400/2,400 scheduled submissions at 720p; optimized conversion | Implemented and measured |
| Compositor adapter | Restricted Weston renderer with a backend control | Linux CPU pixel oracle plus headless/nested Wayland clients | CPU integration validated; native Linux NPU/display validation outstanding |
| Real buffer path | Linear graphics-buffer import/export, synchronization and format conversion | D3D12 shared input/output, byte conversion and native swapchain application with saved switch | Pixel/control checks pass; native display test occluded; DMA-BUF outstanding |
| Work reduction | Damage tiles, occlusion, bounded layer groups, prepared graph variants | Disjoint damage batching in the C ABI; Weston supplies visibility/damage | Initial implementation; sustained native display results outstanding |
| Tahoma benefit | Reproducible concurrent inference and desktop traces | Two five-round GPU/NPU/CPU/idle application comparisons, including streaming | Measured; ~1–1.6% median paired gains do not establish the product target |
| Wider operation coverage | Filters, scaling, transforms, supported app adapters | Native sampling and optimized resize tested on GPU/NPU; 72 Weston transform/oracle cases | Sampling/scaling implemented; wider operations and native display coverage outstanding |
| Broader platforms | Additional NPU providers and Windows integrations | Per-device/OS/runtime evidence with recovery tests | Planned |

## First integration target

Start with SDR premultiplied RGBA, bounded layer counts, and explicit fallback
decisions. Keep existing display/media hardware paths. Build a nested compositor
for correctness, then validate resource relief with standalone output. The outer
compositor in a nested setup can otherwise hide or reverse the intended benefit.

Before calling a mode ready for normal desktop use, measure complete frame costs,
tail latency, missed presentations, color/coverage accuracy, and recovery. The
initial 8 ms processing budget at 1080p60 is a provisional target that leaves
presentation margin; a short graph benchmark cannot establish this milestone.

Before claiming Tahoma acceleration, alternate at least five paired runs with
identical model, prompt/context, desktop trace, output rate, and power mode. Include
CPU composition and idle desktop controls. Record TTFT, throughput, tail token/frame
latency, memory, and available power/engine telemetry. Follow promising results
with a sustained run. A repeatable 10% improvement is a provisional product gate,
not an achieved result or a fundamental viability boundary.

## Broader GPU functions

Promote an operation only after proving its semantics, efficient device mapping,
real integration, and resource benefit. Window composition does not move client
rendering. Video effects do not replace codecs. Triangle arithmetic does not
establish a shader compiler. These are separate expansion tracks within the same
switch/provider architecture.

Investigate custom NPU kernels when a measured graph bottleneck justifies them.
The first runtime uses the installed vendor driver and supported graph interface.
Do not couple every integration to an experimental compiler ABI.

See [the coverage ledger](coverage.md) for the full set of GPU functions and the
specific implementation/integration gaps. The live harness is the first application
adapter and does not replace system composition.
