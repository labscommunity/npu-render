# Windows graphics coverage required for full offload

Updated 2026-10-20. Improving desktop animation alone will not produce the
requested general switch. The current package v19 adapter has deliberate API
limits in addition to its performance and lifetime limits.
The [source inventory](../validation/2026-10-20/native-api-coverage-v1/README.md)
finds 21 callbacks still explicitly rejected out of the 101-entry D3D10 table.
The 80 overridden entries must not be interpreted as an 80/101 feature-coverage
score: several accept only null state or restricted arguments.

| Boundary | Current source or measured behavior | Work required before claiming broad migration |
| --- | --- | --- |
| Device/API negotiation | Only the listed D3D10.0 DDI versions are accepted. The reported 10_0 pipeline level is explicitly not a conformance claim. D3D11-runtime fixture success uses this restricted path. | A tested capability contract, required shader/resource features and real application coverage. No D3D12/Vulkan NPU implementation is qualified by this adapter. |
| Shader stages | Bounded vertex/pixel parsing and NPU execution exist. Geometry-shader creation remains rejected; binding accepts only null geometry state. | Implement and qualify the required instructions, stages, resource access and execution limits. Inventory shaders from each target application. |
| Depth/stencil | Non-null depth/stencil targets are rejected. Depth-view create/destroy/clear entries remain rejected. | Actual depth/stencil storage, ordered testing/writes, clears and independent image tests. Setting a depth-state object is insufficient. |
| Multisampling | The format query reports only one sample. Resolve remains rejected. | Multisample storage, coverage, sampling, resolve and corresponding NPU/image tests. |
| Queries and predication | Query creation, begin/end and readback remain rejected. Predication accepts only a null query. | Correct completion/visibility semantics and bounded synchronization; preserve map/present ordering. |
| Stream output and automatic draws | Stream-output targets accept only null resources. Geometry-with-stream-output and automatic draws remain rejected. | Implement required storage, output ordering and draw-count semantics. |
| Resources and formats | Render targets are limited to RGBA8/BGRA8 UNORM. A8 sampling and a restricted R8_UINT buffer view exist. Texture SRVs select one 2D mip and one array slice. Generic dynamic-resource mapping and mip generation remain rejected. | Expand formats, views and update paths from concrete application requirements; test hazards, aliasing and lifetime. |
| Presentation | Present and identity rotation are bound. Gamma caps, display mode, priority, residency and DXGI blit callbacks explicitly reject. The physical bridge still uses GPU presentation/copy work. | Qualify the required presentation paths and measure remaining iGPU activity. Count scanout separately from graphics execution. |
| Admission and lifetime | One normally started test application works through a finite broker. The broker serves one client and leases expire; existing admission objects keep an immutable lease copy. | Multi-client broker, bounded renewal/revocation, session and worker transitions, and tests with ordinary sandboxed applications. See [admission design](automatic-application-admission.md). |
| Browser, video and games | No broad application or codec qualification follows from the controlled desktop fixture. | Run named applications and account for their graphics, decode/encode and processing paths separately. Preserve unsupported-operation receipts. |
| Tahoma and selection policy | No native-desktop GPU-relief or Tahoma benefit is measured. | Verify that Tahoma remains on the Intel GPU while selected graphics clients use the NPU; compare matched workloads, frame rates and power conditions. |

The implementation order remains: make the existing desktop route responsive,
qualify sustained operation and recovery, extend admission, then implement and
test missing graphics features against named workloads. A browser/game/video
claim requires evidence for that application's actual paths; successful DWM
composition cannot substitute for it. The
[acceptance plan](desktop-offload-plan.md#ordered-gates) tracks the complete goal.

This is a source-derived engineering backlog. It does not establish that each
missing feature is fast enough on the NPU, nor that the current adapter complies
with a complete Direct3D feature level.
