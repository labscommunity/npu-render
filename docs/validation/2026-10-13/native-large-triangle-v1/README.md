# General textured draws on the native NPU path

Native desktop trial v5 executes **69 NPU operations with 69 delivery ACKs**,
including visible textured draws, before DWM exits on another rejected operation.
It produces **zero complete native desktop frames**. Recovery, exact process
permission restoration and memory-integrity checks pass. Delta's ordinary
physical desktop remains on the iGPU. No Tahoma performance comparison runs.

## Addressing and request changes

Trial v4 first confirms that the segmented affine sampler from the preceding
[investigation](../native-texture-address-v1/) executes within actual DWM. It
then rejects a 24-vertex draw using the same large source. That trial completes
20 NPU operations and exits before frame readback.

An independent RGBA FP32 `Gather` probe reproduces the scalar probe's address
wrap at index 2,097,152 on the tested NPU runtime/driver stack. Separate source
ports with at most 1,048,576 rows and integer range masks pass exact comparisons
on both boundary and 5,311,488-row inputs. Native-routing-v9 applies that approach
to the general normalized texture sampler. Typed loads retain their old limit.

ABI 18 admits a larger single normalized source for general triangle draws.
It retains the 32 MiB packet ceiling, existing target limits and legacy blend
payloads. Resource-table packets retain their older per-resource limit. Twelve
negative cases reject malformed shapes, invalid legacy identities and excess
capacity. The isolated candidate and its source patch are archived here; it is
not promoted to the default runtime.

## Validation and its limits

The captured 24-vertex draw passes CPU/NPU replay and the local worker transport
with identical 23,296 output bytes. **It covers zero samples**, so this capture
establishes request acceptance and vertex/projection behavior, not visible
large-texture sampling.

A separate test performs that missing check: an 8,388,608-pixel texture, point
and linear filtering, clamp/mirror/border addressing, and two different contents
through each executor. All 24 CPU/NPU cases and 3,072 component comparisons are
exact. Inputs cover chunk/address boundaries, final texels and border transitions.
Four existing triangle regression runs pass, as do the component worker and
ordinary registered Direct3D tests. The updated signed package is version
0.0.1307.0; its temporary installation and trust are removed after each trial.

## Remaining native failure and performance

Live trial v5 advances beyond the earlier failure and executes visible draws,
including a 24-vertex draw with 1,663 covered samples. It eventually rejects a
157-word pixel shader with quad derivatives, a 2880×1800 source and a 2048×68
target. Isolated replay of that next captured draw already passes on CPU and
NPU with exact pixels and 138,720 covered samples. The native resource-table
admission/transport limit still needs extension and qualification.

Correctness does not imply interactive performance. One live affine draw takes
about **11.9 seconds**. The next captured shader's NPU replay takes about **3.17
seconds per draw**, plus about **9.13 seconds of model preparation** in that run.
The independent sampler copies about 134 MB of inputs per invocation. These are
specific diagnostic measurements, not a desktop frame rate or a Tahoma benchmark.
Resource reuse and transfer/dispatch costs remain substantial work.

The [report](report.json), [manifest](raw-manifest.json), source patch and test
sources preserve 217 remotely hash-verified text artifacts. Desktop pixels,
executables, model files and caches remain excluded. Full native desktop output,
physical presentation, input integration and useful GPU resource relief remain
unproven.
