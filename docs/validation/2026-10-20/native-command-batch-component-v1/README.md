# Ordered NPU command batches: exact component and measured gains

This isolated experiment executes up to 32 ordered solid RGBA8 rectangles in one
kernel submission. Each command supplies integer pixel bounds, a packed color,
copy/source-over blending and a channel mask. The target may be RGBA or BGRA.
Coverage, arithmetic and per-command quantization execute on the NPU; the host
packs metadata and transports the initial/final pixels. It does not implement
textured draws, arbitrary geometry or a Windows command queue.

Three kernels preserve the same operation contract. V1 visits each pixel and
checks every command. V2 visits covered row intervals in command order, retaining
intermediate destination pixels in NPU-visible output storage. V3 processes four
aligned pixels per vector, computes enabled channels, and handles scalar heads
and tails. Their code sizes are 1,792, 1,984 and 2,992 bytes, within the unchanged
18,256-byte carrier allocation. Metadata and compiler-owned buffer bounds remain
unchanged.

V1 and V2 each pass all 72 original full-span fixtures on NPU. V3 passes those
72 plus 48 cases covering zero/opaque alpha, channel saturation and edge values.
All kernels also pass the corresponding independent CPU integer reference, and
original Softmax checks before/after NPU execution. No CPU fallback is used for
the tested rendering outputs. The V3 model SHA-256 is
`e4a863410d6e629cd4f5ebcd955201a9024b2e75e6e6f6b19366fe582f6aaf26`.

The final comparison uses matched A/B/B/A runs for three separate questions:
submitting single commands versus batches, V2 versus V3 pixel processing, and
unbalanced versus balanced assignment across six compiler-owned spans. Every
complete image matches an independent integer oracle. Warmups are separate.

| Synthetic image and commands | Separate V3 submissions, mean | Batched V3, mean | Time reduction | Calls per image |
| --- | ---: | ---: | ---: | --- |
| 64×64, 8 rectangles | 8.143 ms | 1.065 ms | 86.92% | 8 → 1 |
| 256×256, 32 rectangles | 46.781 ms | 6.119 ms | 86.92% | 32 → 1 |
| 1024×768, 32 rectangles | 242.047 ms | 45.926 ms | 81.03% | 96 → 3 |

Times include host packet preparation, NPU request waits and output transport;
oracle construction and final image comparison are excluded. There are two runs
per side, not a sustained rate or thermal study. The large case still exceeds a
33.3 ms frame budget. Fixed tensor byte counts are logical transport quantities,
not measured DRAM bandwidth.

In separate paired comparisons, V3 reduces balanced large-image batch time by
58.05% relative to V2. Balancing helps the 256×256 case by 74.25%; it changes the
large case by only 0.48% because the full chunks already occupy all six spans.
This explains why counting fewer launches alone was insufficient. Earlier
comparisons are retained: V1 batches took about 438 ms for the large case, and V2
about 113 ms, in their respective runs.

The first comparison harness rejected `EXECUTION_DEVICES` because it treated a
string as a list of characters. No benchmark case ran. V2 of that harness uses
the same explicit string/list normalization as the qualified component harness;
the original failure is retained. Later comparisons retain the model pin and
require an execution device of exactly NPU/NPU.0.

The 98-file manifest retains all three kernels and device proofs, CPU fixtures,
all four comparison attempts, build identities and source-generation scripts.
Model blobs, executables and compiled caches remain excluded. The new batch
kernel is not connected to the command ledger or Windows driver yet; resource
ownership, recovery, real captured draws, display delivery and Tahoma benefit
require separate integration and qualification.
