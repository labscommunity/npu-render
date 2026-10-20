# Combined vector fill and compiled-graph turbo

Worker31 preserves every image and reduces aggregate parent rendering time by
**5.6630%** against worker26. Both provider-recovery scenarios pass. This meets
the five-percent threshold for a separate physical trial; it is not a desktop
frame-rate or Tahoma result.

Worker sources exactly match vector-fill worker27. Linked provider libraries
exactly match compiled-turbo worker30. The larger complex-shader batch from
worker29 is excluded. The combination is measured independently rather than
adding earlier component gains.
Worker SHA-256:
`b4429cb12917acb2cc9534cc97227086f3a24cded8ef10ba572d334571320c3b`.

All 174 selected requests, 972 full-replay requests and 3,888 comparison images
pass. Each timed phase has one provider generation and no provider failure.

| Phase | Worker | Parent time | Child time |
| --- | ---: | ---: | ---: |
| A1 | 26 | 118,386.9776 ms | 106,025.8352 ms |
| B1 | 31 | 111,716.8619 ms | 99,466.8126 ms |
| B2 | 31 | 110,357.7289 ms | 98,142.3077 ms |
| A2 | 26 | 117,018.6024 ms | 104,581.4299 ms |

Across 1,944 matched pairs, parent time falls from 235,405.5800 to
222,074.5908 ms. There are 453 slower individual pairs. These are two runs per
worker, not 1,944 independent timing experiments.

Both recovery scenarios return 31 exact images after an injected provider
exception or owned-child termination. Each records two generations, one failure
and one NPU retry. Before the first run, the test was extended with original
ticket 589 so the changed program model executes after restart as well as before
it. Both scenarios record three program draws before restart and one afterward.
This correction and its source hashes are retained. Real device loss is not
claimed.

`artifact-sha256.json` identifies 144 artifacts. Remote artifacts total
19,271,165 bytes before lossless compression. The worker runs after Windows
servicing, in the same session-zero task context as its baseline. No interactive
desktop or Tahoma workload is measured here.
