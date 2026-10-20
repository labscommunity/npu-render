# Windows resource boundaries and failed desktop diagnostic

The resource-observation UMD builds and passes its lease, unleased rejection and
trace-lifecycle contracts. Driver package 21 (`0.0.1321.0`) stages, recovers and
passes the normal registered Direct3D test with worker 24. Rendering remains
synchronous; the changes record identities and access boundaries only.

The live trial **fails**. On parent dispatch 6, OpenVINO reports
`ZE_RESULT_ERROR_DEVICE_LOST` while rendering a 2880×1800 draw. The captured
20,738,104-byte child request has SHA-256
`55d07a459f2c54eed7a18694c3f08b7eee3d6917412d066249ea31835ff40b87`.
This fault was not injected. The parent starts generation 2, retries on the NPU
and continues. The later shared-application validator assumes exactly one child
generation and raises an assertion. Bootstrap removes the indirect display;
the relay then observes that its pinned producer exited. The relay failure is
therefore downstream of the validation failure, not evidence that pixel delivery
caused the original device loss.

The relay reports ten published frames before termination. This failed run does
not replace the passing live60 baseline or qualify sustained rendering. Native
recovery, cursor cleanup and observer cleanup pass; diagnostic packages and
temporary certificate trust are removed, and memory integrity remains enabled.
The device-loss cause is unresolved.

The archived prefix contains 2,651 closed resource-operation scopes and 587
unique joins to completed parent requests. Within DWM, 2,196 closed scopes
include 461 draws, 561 maps, 559 unmaps, 21 flushes, six presents and six backing
rotations. Logical versions and the recorded backing rotations are consistent.
The analysis partitions identity by process and QPC epoch, checks serial
continuity, retains incomplete scopes and explicitly marks external aliases.

There are 159 observed same-target draw runs. Of these, 78 contain multiple
draws, comprising 380 commands; the largest contains 36 draws and 111 distinct
input versions. Intervening maps of unrelated private inputs are counted under
an **owned input snapshot assumption**, not treated as proof of safe deferral.
Shared resources remain potentially aliased. The DWM trace reaches its roughly
4 MiB budget and is a finite prefix; no complete-run coverage is claimed.

V1 does not observe shader-view hazards, and copy-region events are unscoped.
Consequently these runs are opportunities for further batching work, not an
admissible command schedule. V2 adds those scopes and device destruction events.
No work is deferred by either diagnostic, and no speedup, GPU relief or Tahoma
benefit is demonstrated.

The 36-draw group references 45 logical texture versions totaling 48,334,488
bytes, excluding target backgrounds and row padding. This is a logical input
footprint, not physical residency or memory traffic. Only two of the 78 multi-draw
groups have private targets, no flagged external input aliases, and at most
64 KiB of texture-version bytes. A small solid-color batch is therefore not a
representative complete desktop batch.

The manifest preserves 192 artifacts: sources and build contracts, signed
package metadata, registered execution, failed live evidence, cleanup, logical
resource traces and the analysis. Binary packages, pixel payloads and model
weights remain outside this archive.
