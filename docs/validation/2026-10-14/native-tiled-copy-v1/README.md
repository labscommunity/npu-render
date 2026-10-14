# Bounded opaque copies and remaining native state failure

Full desktop offload is not working. This investigation qualifies a faster draw
path but the resulting native desktop trial still fails. Tahoma was not tested.

## Qualified draw optimization

Source v18 splits eligible opaque copies into bands whose input width × height
is at most 65,535 pixels. Pixel extraction, channel exchange, alpha replacement
and merging within each changed band execute on the selected backend. Host work
transfers complete bands and copies existing unchanged rows. The specialization
retains the conservative coordinate/quantization admission proof from the
[previous experiment](../native-runtime-state-v1/README.md).

Two complete NPU suites pass: 2,304 inference calls and 573,456,384 checked color
components across full and clipped rectangles, channel exchange, checkerboards
and random inputs. The corresponding CPU suite also passes. Seven unsupported
cases are rejected before compilation in each suite. This is bounded fixture
coverage, not a general Direct3D conformance result.

The integrated executor prepares its large general sampler only when a draw
actually needs it. A separate test repeatedly switches the same executor between
the optimized copy and general modulation/blending. All 60 draws and 737,280
component checks pass on CPU and in two NPU runs. Compilation counters verify that
the general model is prepared on first demand and reused afterward. Two earlier
test harness builds failed because of Windows header macros and a missing helper;
their source and compiler logs are retained.

All twelve integration regression checks pass with batch capacity 65,535. Ten
additional checks pass with the original default batch policy. On the captured
2880 × 1800 desktop draw, three integrated NPU executions take 327.632, 248.410 and
310.119 ms, with 254.869 ms of initial setup reported separately. Output matches
the existing CPU reference exactly. The earlier general sampler took about 7.5
seconds per operation. Neither timing establishes desktop frame rate.

## Native desktop trials remain failures

| Trial | Candidate | Result |
| --- | --- | --- |
| Live v6 | Shared Core, source v17 | 129 acknowledged NPU operations, no presented frame, then device loss. Windows records a `dwm.exe` crash in `dwmcore.dll` at 15:48:30 UTC. Native cleanup passes. |
| Live v7 | Tiled copy and deferred general sampler, source v18 | 188 acknowledged NPU operations and one matching source-to-present submission, then invalid projected triangle bounds. Native cleanup passes. |

Live v7's physical presenter verifies an exact backbuffer copy, but the submitted
frame does not contain the controlled stimulus color. Both presenter and stimulus
child processes fail when recovery removes the indirect display. The coordinator
now checks those child results and controlled content explicitly. Scanout, input
handling, sustained rendering and residual GPU cost remain unqualified.

The failed projection reports bounds `[-1107296256, -1149239296, 0, 0]`. The exact
24,992,136-byte request succeeds in a fresh worker, with one NPU operation and one
delivery acknowledgment. Its output is preserved privately; this replay does not
yet independently verify pixel correctness. The result again points toward prior
execution state, without establishing a driver/runtime root cause.

## Next diagnostic

Record the ordered request stream and hashes of successful outputs on Delta,
then replay it through an isolated worker. This should make accumulated-state
failures reproducible without repeated desktop admission. Capture is bounded,
private and excluded from Git; its overhead invalidates performance comparisons.
The conservative default raster-batch policy will be used for this recording.

The general pixel executor also recopies immutable textures for every raster
batch. A scoped upload-reuse candidate is being prepared separately. No benefit
from that unqualified candidate is claimed here.

## Evidence

`native-routing-v18.patch` applies after source v17 in the previous investigation.
`sources/` contains the isolated tile/transition tests and preparation scripts.
The archive contains 254 text artifacts, totaling 1,449,388 bytes, verified against
hashes generated on Delta. Binary captures, desktop pixels and model caches are
excluded. See [report.json](report.json) and [raw-manifest.json](raw-manifest.json).
All candidates remain outside the default runtime; the repository remains private.
