# Milestones toward the graphics switch

The end goal is to select an NPU graphics mode once and have participating
integrations move as much eligible work as possible, preserving visual and frame
delivery requirements while freeing useful GPU capacity.

| Milestone | Deliverable | Acceptance evidence | Status |
|---|---|---|---|
| Device feasibility | Deterministic composition and restricted triangle graphs | Independent checks on Panther Lake NPU, including reported execution devices | Original probe complete |
| Shared runtime | Provider contracts, device selection, prepared switch, owned outputs | 108 Python checks, native pixel tests, real device switching and loaded-driver qualification | Implemented |
| Live application | Animated software window, async switch, frame-stage timing | Windows console session; 2,400/2,400 scheduled submissions at 720p; optimized conversion | Implemented and measured |
| Compositor adapter | Restricted Weston renderer with a backend control | Linux CPU pixel oracle plus headless/nested Wayland clients | CPU integration validated; native Linux NPU/display validation outstanding |
| Real buffer path | Linear graphics-buffer import/export, synchronization and format conversion | D3D12 shared input/output, byte conversion and native swapchain application with saved switch | Pixel/control and active-desktop 720p presentation checks pass; scanout timing and DMA-BUF outstanding |
| Work reduction | Damage tiles, occlusion, bounded layer groups, prepared graph variants | Disjoint damage batching in the C ABI; Weston supplies visibility/damage | Initial implementation; sustained native display results outstanding |
| Tahoma benefit | Reproducible concurrent inference and desktop traces | Native D3D12 baseline, graph variants, sustained warmup, stage-isolation controls and telemetry | Battery now charges under load; unchanged-executable repeat still regresses 4.7%. NPU-only activity costs 3.3% versus idle; useful inference benefit remains unproven |
| Wider operation coverage | Filters, scaling, transforms, supported app adapters | Native sampling and optimized resize tested on GPU/NPU; 72 Weston transform/oracle cases | Sampling/scaling implemented; wider operations and native display coverage outstanding |
| Cooperating Windows handoff | Retire owned GDI windows and compose their surfaces on NPU/GPU | Exact offscreen/screen oracles, switch retention, original-window restoration and concurrent Tahoma measurements | Bounded handoff implemented; outer DWM and arbitrary applications remain |
| Windows command executor | Direct3D runtime invokes NPU-backed user-mode driver callbacks | Independent pixel oracle, explicit backend attribution, texture update and rejection checks | Restricted process-local DDI laboratory implemented; shared allocations, fences, DWM selection and system driver outstanding |
| Broader platforms | Additional NPU providers and Windows graphics driver | Per-device/OS/runtime evidence with recovery tests | Direct-LUID probe finds NPU but installed driver rejects Direct3D device creation; system graphics adapter remains unimplemented |

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

The [power-corrected controls](validation/2026-10-07/power-retest/README.md) show
that comparable host waiting does not reproduce the interference from NPU execution.
Prioritize reducing per-frame work and intermediate/transfer costs, then retiring
existing compositor work at a real integration boundary. Compare equal visual work
and apply shared damage/caching optimizations to both GPU and NPU baselines; a faster
NPU call alone is not the resource-relief acceptance criterion.

The [retained Windows follow-up](validation/2026-10-08/retained-windows/README.md)
removes recurring foreground host uploads and replaces costly row copies with
rectangle copies. It improves Tahoma by 2.1% relative to staged NPU in the paired
median, but remains 3.1% below DWM in all five pairs. Next reduce the three
full-frame blend passes or their processed area, applying the same optimization
to both devices. Arbitrary application integration and a net Tahoma gain remain
separate acceptance requirements.

## Broader GPU functions

Promote an operation only after proving its semantics, efficient device mapping,
real integration, and resource benefit. Window composition does not move client
rendering. Video effects do not replace codecs. Triangle arithmetic does not
establish a shader compiler. These are separate expansion tracks within the same
switch/provider architecture.

Investigate custom NPU kernels when a measured graph bottleneck justifies them.
The first runtime uses the installed vendor driver and supported graph interface.
Do not couple every integration to an experimental compiler ABI.
Indexed-sampling profiling now supplies that justification: Gather consumed
19.231 ms in a roughly 21 ms profiled call. Follow the
[Panther Lake kernel investigation](research/shave-kernels.md) through toolchain,
invocation-contract, small-kernel and addressing checks before runtime integration.

See [the coverage ledger](coverage.md) for the full set of GPU functions and the
specific implementation/integration gaps. The live harness is the first application
adapter and does not replace system composition.
