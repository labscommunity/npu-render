# Broader four-sample desktop rendering

The direct NPU renderer now matches all 24 recorded draws using the pinned
four-sample desktop shader. Previously it admitted three. All 96 comparisons are
exact: each recorded input plus three generated source/background images.
Geometry includes both triangle orders, fractional bounds, partial texture
regions, different target sizes, constant translucent color and partial batches.
The host checks geometry produced by NPU vertex/projection execution, copies
source bands and assembles output. Coverage, sampling, blending and quantization
execute on the NPU. Untouched background rows are copied without recomputation.

The revised six-span kernel passes 96 independent source-window fixtures,
including zero-length and partial output batches, with original carrier checks
before and after. The host verifies the exact shader, model hash, transport shape,
geometry and source capacity. Five negative admission checks accompany each
recorded comparison. No driver or desktop change occurs in these tests.

The first host attempt rejects a legitimate missing scissor rectangle. The second
accepts that case but rejects the full-size source band because its conservative
packing margin exceeds capacity. Both failed runs are retained. The third handles
the absent scissor as the full target and uses a smaller conservative band margin;
the NPU still rejects every attempted sample outside the supplied source window.
It passes all 24 captures and changing-image comparisons.

Timings are alternating component comparisons against the existing NPU renderer,
not desktop frame rates or matched full-system benefit. The full-size draw uses
29 calls; small window draws use as few as three. Some previously multi-second
draws take roughly 70–85 ms. Initial setup, host activity and content affect these
measurements. One CPU-only inspector build overlaps early comparisons; there is
no controlled power or whole-system performance claim.

This expands one shader class. It does not establish responsive full desktop
rendering, ordinary application migration, reduced GPU utilization or Tahoma
benefit. The integrated worker replay is recorded separately. Models, binaries
and captured pixel payloads remain excluded from the repository.
