# Bounded source windows and corrected capacity guard (2026-10-15)

The four-sample renderer remains exact when it reads a bounded source strip from
NPU local memory. It does not improve performance in the captured request.

| Warm iteration | Existing renderer | Windowed fused renderer | Different components |
| --- | ---: | ---: | ---: |
| 1 | 9,038.2882 ms | 2,065.6709 ms | 0 |
| 2 | 9,172.2157 ms | 2,069.6885 ms | 0 |

Each comparison checks 3,145,728 components. All three output hashes, including the
initial iteration, equal the retained baseline hash
`c600b795e5f9684f98ceb2b1376c94456ca978355aa53323993c8a4842da644c`.
Both vertex processing and projection execute on NPU. Host work packs unchanged
source strips and output buffers. This diagnostic admits only the verified
captured geometry; each strip follows its fixed scale and output band. Every NPU
sample checks the declared source window, and an insufficient window emits `-1`
for the affected output rather than reading beyond it. No live integration or
Tahoma test occurred.

The previous full-source candidate takes about 1.94 seconds in its warm trials.
These separate runs do not establish a small statistically reliable difference,
but the source-window experiment clearly does not supply the required order-of-
magnitude improvement. Transfer volume alone is not a sufficient explanation for
the remaining draw cost.

Before direct replay, 32 random full-texture fixtures pass 2,097,152 exact component
checks against the independent full-source reference. A deliberately misplaced
window yields 65,536 explicit rejection values. Original Gather health checks
pass before and after. The input carrier has 1 MiB source storage and 16,384 output
pixels, held in local memory; seventeen offline admission/rejection checks pass.
The first crop kernel build fails because a source-edit replacement omitted the
crop declaration. The first hardware harness then rejects a string-valued device
property before custom execution. Both failures are retained; corrected source
and normalized device reporting pass.

The full-source v2 kernel also corrects the capacity expression to account for
its complete 160-byte header. It passes all 4,194,304 exact components across the
seven previously qualified shapes, with original-graph health before and after.
Its ELF SHA-256 is
`c3842d4cd77f0d8fcd379e9316bc763a5b8c51872fac2c6949452116ec9369e3`.
The crop kernel ELF SHA-256 is
`219c987fbfb116062d43181ce08a37d2eb91e037569f6dc400081d36d226c7a8`.

The initial parallel-carrier experiment uses three and six independent original
Gather operations with `ReferenceSW` and three requested NPU tiles. All original
outputs pass, but exported invocation metadata places every operation on tile 0.
Requesting three tiles does not by itself demonstrate parallel execution.
Intel's pinned compiler resource definitions declare two SHAVE ACT executors per
NPU50XX tile; Delta reports three tiles. Further carrier/layout qualification is
required to use those resources with the custom renderer.

`artifact-sha256.json` authenticates 80 retained evidence/source files. Source
windows, captured pixels, toolchain DLLs, kernels and compiled graphs remain
excluded from the repository. The live renderer and ordinary desktop are unchanged.

Archived Python sources use `.py.txt` to preserve their original bytes without
treating measurement scripts as maintained runtime modules. Remove the final
`.txt` suffix when reconstructing a retained experiment.
