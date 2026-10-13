# Coverage-stage performance experiments

These isolated probes preserve exact coverage on the tested NPU cases and explain
part of the renderer's cost. They do not establish physical desktop offload or a
Tahoma improvement. The integrated desktop-request replay is a separate experiment.

| Experiment | NPU oracle | Warm comparison, first round excluded |
| --- | --- | --- |
| Coverage plus output-merger fusion | 281,600 values pass | Sum of 11 pairs decreases 4.8%; medians 8.951 vs 9.179 ms |
| Compact coverage, interleaved inputs | 348,160 values pass | Sum decreases 48.9%; medians 3.083 vs 6.201 ms |
| Compact coverage, component-contiguous inputs | 348,160 values pass | Sum decreases 54.7%; medians 2.894 vs 6.415 ms |

Each is one short alternating-order comparison with a visible warming trend.
Times include host admission, packing, inference and output validation. They are
not pure hardware times. All three GPU attempts fail their oracle; no GPU fast
path is qualified. CPU suites pass. The initial fusion build omitted the explicit
OpenVINO target dependency for a header that exposes its types; the corrected
clean v2 build and original compiler error are both retained.

Compact coverage translates each sample and divides each axis by an exact positive
power of two. Admission requires each normalized axis span to be at most 4,095.
This preserves orientation, edge signs, degeneracy and top-left edge ownership.
Products of coordinate differences are exactly representable in FP32 below 2^24;
subtraction preserves the required sign and zero predicates. NPU precision markers
are serialized and checked. Original integer sample positions remain device
inputs for scissor comparisons. Host normalization and admission are explicit;
the NPU computes coverage. Inadmissible geometry is rejected by these standalone
probes; the proposed runtime retains its existing exact NPU coverage path.

The coverage oracle uses independent signed 64-bit arithmetic. It covers six
culling/winding combinations, twelve geometry scenarios including near-cancelling
determinants, translated coordinates near both admitted extremes, anisotropic
scales, degenerate geometry, scissoring and edge ownership. Rejected domain and
invalid-coordinate cases are followed by successful execution. The last twelve
rounds compare against the existing wide-arithmetic implementation as well.

The instrumented 4,096-sample fused graph passes another 245,760 values. Its final
profile records 753 layers: 721 SHAVE/vector entries and 32 DMA entries. Task
statistics report an 8.941 ms span, 5.401 ms software/vector union, 2.367 ms DMA
union, 0.271 ms overlap, and no recorded DPU work. These are one instrumented
inference's statistics, not an assertion about every rendering operation. Layer
sums are not elapsed time, profiling adds overhead, and the workpoint metadata
reports `INVALID`; the reported frequency is not treated as verified. The raw
2.1 MB Perfetto trace remains in the ignored evidence archive with a verified hash.
Intel documents reconstructed layer timing and profiling overhead in its
[profiling guide](https://github.com/openvinotoolkit/npu_compiler/blob/develop/guides/how_to_use_profiling.md).

The 65,536-sample instrumented case fails at inference with
`ZE_RESULT_ERROR_DEVICE_LOST`. It is not qualified. A subsequent fresh-process
NPU add probe passes 48 values without a physical device restart. The first health
probe had stopped before inference because Python returned `EXECUTION_DEVICES` as
a string; its original assertion failure is preserved alongside the corrected,
explicitly checked property handling. Recovery of the small probe does not qualify
the failed larger graph or establish a universal safe batch size.

Thirty-seven downloaded files are checked against remote SHA-256 receipts. Source
manifests match the clean-build records. Five patches preserve the experimental
lineage from the previously qualified `queue-worker-v4` source; the runtime source
has not been replaced by these standalone experiments.
