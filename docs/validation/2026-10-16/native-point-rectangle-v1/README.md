# Direct point-sampled rectangles on the NPU

A two-phase kernel implements the existing affine rectangle contract directly on
six compiler-owned NPU spans. Phase zero computes snapped coverage and point-sample
addresses. The host validates those addresses and transports packed texels; phase
one performs modulation, source-over blending, format conversion and byte rounding.
The kernel retains the old rectangle interpolation rules instead of routing these
draws through generic triangles. Its code occupies 3,984 of the qualified 18,256
bytes, with no change to model metadata or carrier bounds.

All 233 CPU and NPU fixtures pass: 192 valid and 41 invalid payloads. Thirty-nine
six-span device invocations include one padding repeat. Eight original-carrier
checks pass before and after the custom kernel. The first local fixture report
is retained as failed: four supposed invalid cases changed only one dimension to
8192, leaving a valid total area. The corrected fixture makes both dimensions
8192 to exceed the area bound. The kernel itself is unchanged by this correction.

Inspection of 605 structurally eligible captures admits 37 legacy point-rectangle
draws from two shader groups. Both adapter revisions pass all four image variants
of every admitted draw: 148 variants each, matching both the original NPU renderer
and the original rectangle graph executed on CPU. All 37 original hashes are
preserved. The CPU graph is confined to the test harness; the separate NumPy oracle
checks the kernel arithmetic independently. No CPU renderer is added to the worker.

The first adapter always uses the direct kernel. Across 111 warm pairs, summed
complete draw time falls from 12,750.614 ms to 6,002.048 ms, with 28 slower pairs.
A large 2880-by-1800 draw is roughly twice as fast, but a small plain-copy case is
about three times slower because the previous renderer already has an efficient
crop/copy path.

The second adapter preserves the qualified crop path whenever its existing mapping
proof accepts the draw. All 148 images remain exact, and the small plain-copy
regression disappears. However, retaining that path for large blended copies
also retains their cost: summed warm time is 9,610.526 ms versus 12,764.129 ms on
the original renderer, with 53 slower pairs. There are 124 preserved-crop variants
and 24 direct-kernel variants. These are separate alternating-order component
comparisons; the cross-run sums do not establish a matched v1-versus-v2 benchmark.

This result motivates a subsequent route policy based on blend semantics and
estimated row-batch count. Neither revision is presented as uniformly faster.
Worker integration, live response and a Tahoma benefit are separate gates.
The existing crop performs a checked mapping proof on the host and transfers
bands; changed pixel operations still execute on the NPU. The new direct path
keeps coverage, addressing and shader arithmetic on the NPU.

Model SHA-256:
`b7caa092d2581fec8cec075815ab40057a9f3f5a654b5f0abbe7893c11a74908`.
The archive pins 234 source and evidence artifacts, including the initial fixture
failure and both complete-image comparisons. Private image data, model blobs and
compiled binaries remain excluded. The earlier v1 capture coordinator stopped
at a preceding worker-audit failure before building or running this comparison;
that report is also retained. `summary.json` derives the comparison totals.
