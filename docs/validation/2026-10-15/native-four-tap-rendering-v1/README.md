# Fused captured four-sample desktop draw (2026-10-15)

A custom Panther Lake kernel now reproduces the retained slow desktop draw
exactly. Three direct comparisons each match all 3,145,728 output components.
Vertex processing, projection, helper-quad derivatives, filtering, modulation,
coverage and source-over blending execute on the NPU. This is isolated replay;
the live desktop provider has not changed and Tahoma was not tested.

| Iteration | Order | Existing renderer | Fused renderer | Pixel differences |
| --- | --- | ---: | ---: | ---: |
| 0 | Existing first | 9,038.0418 ms | 2,686.3342 ms | 0 |
| 1 | Fused first | 9,172.4140 ms | 1,945.8111 ms | 0 |
| 2 | Existing first | 9,166.8129 ms | 1,939.9543 ms | 0 |

The two subsequent trials show about a 4.72× draw reduction. The first iteration
includes lazy setup and a candidate fixture write, so it is excluded from that
comparison. Candidate timing includes NPU vertex/projection, source packing,
inference, output conversion and assembly. Both paths operate on the same saved
request. Provider calls fall from 1,282 to 50. The baseline retains its qualified
texture-upload reuse: 82,944,000 host bytes uploaded, with 26,459,136,000 repeated
host-copy bytes avoided. This does not measure device DMA.

The baseline and candidate output SHA-256 both equal the earlier retained response:
`c600b795e5f9684f98ceb2b1376c94456ca978355aa53323993c8a4842da644c`.
The input packet SHA-256 is
`75d41f0b2edb0ed4a2edd095303d7655dd7c428c12f3dd5098281e81d3acbfeb`.

The underlying kernel first passes 4,194,304 exact components across seven
source/batch layouts, with an independent NumPy reference and original Gather
health checks before and after. These include a 32 MiB explicit source allocation
holding the full 2880×1800 image. Eighteen offline layout/rejection checks precede
hardware execution. Only executable kernel bytes change in the admitted carrier;
DDR source relocation and firmware cache maintenance remain intact.

The first comparison harness failed because OpenVINO rejects requesting a `u8`
pointer from an `f16` tensor. Version 2 accesses its raw storage correctly and also
restores the baseline's texture-upload reuse setting. The failed result is
retained and is not used for timing claims.

A later review found that the v1 kernel capacity expression still subtracts the
old 128-byte header instead of the new 160-byte header. All exercised payloads
have ample unused capacity, so this did not affect the recorded results, but it
could admit an input that exceeds the declared span by 32 bytes. Corrected v2
source and qualification are required before promotion. No live path uses v1.

Roughly two seconds per draw remains far above the responsiveness target. Next
experiments address on-chip source windows and parallel execution, retaining
exact comparison as the promotion gate. Application coverage, sustained live
responsiveness, remaining GPU work, Tahoma A/B and a usable switch remain open.

`artifact-sha256.json` authenticates 52 evidence/source files. Private captured
pixels, toolchain DLLs, compiled kernels and device graph binaries are excluded.
The harness links the unchanged `native-routing-v45` static libraries; their
hashes and each clean build are retained.

Archived Python sources use `.py.txt` to preserve their original bytes without
treating measurement scripts as maintained runtime modules. Remove the final
`.txt` suffix when reconstructing a retained experiment.
