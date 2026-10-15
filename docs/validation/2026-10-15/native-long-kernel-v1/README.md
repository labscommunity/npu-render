# Sustained desktop delivery and reusable Panther Lake kernel tools

The native v36 trial passes the controlled thirty-minute delivery gate and
restores Delta's ordinary topology, windows, pointer and access controls. It
records 1,833 NPU acknowledgments, 167 exact matching physical submissions spanning
2,076.05 seconds, and four fresh independently observed color samples near the
end. Last-delivery age is 8.66 seconds. The publisher retries two Windows sharing
conflicts and has no publication timeout. This establishes sustained controlled
delivery, not responsive desktop performance.

The presenter issues 334 GPU copy commands, including 167 validation readbacks.
The independent observer adds 530 measurement copies. Physical scanout remains on
the iGPU. General application device admission still fails; no Tahoma comparison
was run. The sparse controlled stimulus and 60-second freshness bound do not
qualify the 30 fps / 100 ms responsiveness target.

The reusable repository builder reproduces the previously qualified identity
ELF exactly. The repository validator passes original/custom/original NPU checks
for both identity and indexed arithmetic, with 64 changing inputs and 1,024 exact
components per phase. Its source snapshot matches the repository files retained
in the raw evidence. The indexed kernel performs a nontrivial permutation,
multiply, add and clamp; the original graph passes again afterward.

A separate identity test preserves all 4,112 tested raw 16-bit words exactly,
including signed zeros, subnormals, infinities and NaN payloads. Six larger
unmodified carrier graphs also pass their independent output checks, including
full-precision indexed reads. These are prerequisites for direct graphics kernels;
this record does not qualify a custom full-texture sampler or a speed improvement.

The retained v35 cost analysis joins operations by dispatch ticket, not the
repeated client sequence number. Across the whole worker lifetime, 1,253 completed
operations make 42,760 provider requests. The largest takes 9,596.5 ms, makes 1,282
requests and performs no compilation. All 281 recorded disk-cache lookups hit.
These diagnostic counters include startup and cleanup and are not a matched
presentation benchmark. They establish that warm rendering cost must be reduced
alongside cold compilation; overlapping stage timers must not be added together.

`raw-manifest.json` records 236 remotely hash-verified files (8,395,752 bytes).
`source-manifest.json` pins the local preparation, inspection, collection and cost
analysis sources. Captured bytes are preserved without newline conversion.
Compiled graphs, kernels and proprietary toolchain binaries are not distributed.
See [report.json](report.json) for the explicit remaining gates and
[the execution plan](../../../research/desktop-offload-plan.md) for next work.
