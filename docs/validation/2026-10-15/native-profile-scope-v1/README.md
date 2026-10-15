# Profiled rendering cost and scoped texture uploads

Scoped texture uploads reduce preparation plus drawing time by **18.76%** in a
matched 16-draw replay. All 96 outputs across warm-up and A/B/B/A phases match the
recording exactly. This is a component result: the candidate has not passed the
complete stream or a native desktop trial. Full desktop offload and a Tahoma
benefit remain unqualified.

Delta runs the same OpenVINO 2026.4.1 runtime and NPU driver 5540. All measured
phases report AC power, complete disk-cache hits, one provider generation, and no
provider recovery. Both conditions use generic batch 4096, quad batching, and
shared compiled models with capacity 512. Only scoped texture reuse changes.

| Condition | First measured replay | Second measured replay | Mean |
| --- | ---: | ---: | ---: |
| Recopy textures per shader batch | 79,229.766 ms | 78,645.684 ms | 78,937.725 ms |
| Upload once per draw scope | 63,803.431 ms | 64,447.755 ms | 64,125.593 ms |

The scope ends at each draw; new resources and runtime constants remain visible.
CPU and NPU fixtures each check 480 exact components across six resource
generations and reject twelve invalid scope uses. The candidate stays disabled
by default. Its previous implementation had intermittent failures on an older
worker, so these isolated passes do not replace sustained qualification.

## What the profile establishes

Sixteen recorded draws pass with 64,262.498 ms of aggregate drawing time. Actual
synchronous provider inference intervals total approximately 47,854 ms:

| Call site | Inferences | Time |
| --- | ---: | ---: |
| Shared vertex/pixel shader executor | 768 | 29,458.679 ms |
| Raster coverage | 1,755 | 6,764.683 ms |
| Output merger | 1,710 | 5,941.415 ms |
| Color interpolation | 750 | 3,667.025 ms |
| Specialized linear texture draw | 160 | 1,969.383 ms |
| Projection | 15 | 33.390 ms |
| Clear | 1 | 19.296 ms |

All recorded inference counts reconcile with the drawing receipts. The remaining
drawing interval includes host preparation, copies and other work; this profile
does not independently measure each of those costs.

The aggregate `input_bytes` counter is **not host-copy or device-bus telemetry**.
In particular, the specialized linear path counts a full texture binding for each
inference even though it copies that texture once before its band loop. Its
reported byte total cannot be used to infer physical memory bandwidth.

## Candidates and rejected evidence

Packing a synthetic 2880×1800 texture into bytes reduces its input allocation by
75% and preserves the three tested gather patterns exactly on NPU. Mean inference
time increases from 2.843 to 4.798 ms, while repeated host-copy time falls from
11.233 to 3.483 ms. This can help a repeated-copy test but may lose once uploads
are reused. It is not integrated into the renderer.

Keeping uniform shader expressions scalar passes 9,442,300 checked components in
each CPU/NPU baseline/candidate configuration. Nevertheless, its recorded replay
differs at ticket 140 after two matching outputs. The candidate is not promoted.
The exactness check compares against prior successful NPU pixels; diagnosis must
also compare CPU execution before assigning the difference to shader semantics
or the NPU compiler.

The first uniform fixture fails on its CPU baseline because it reuses an executor
after a deliberate invalid lookup. The corrected fixture creates fresh executors
and explicitly verifies that failed executors reject reuse. Both results are
retained. No hardware success is inferred from a corrected CPU fixture alone.

## Reproduction and provenance

The archive contains 141 remotely hash-verified files, including clean-build
receipts, raw failures, timing logs and controllers. `raw-manifest.json` retains
the exact transferred bytes. Source patches form these explicit chains:

- v37 → v39: optional per-call-site profiling.
- v39 → v40: scoped texture uploads, default disabled.
- v39 → v42 → v43: uniform candidate and corrected fixture lifecycle.

The source manifests bind each build to the prepared files. No captured desktop
pixels, model weights or compiled device caches are included. The complete-stream
test, native integration, long-duration interaction, application routing,
residual iGPU cost and matched Tahoma test remain required.
