# Vector memory fill: complete renderer comparison

Worker v27 preserves every recorded image and reduces aggregate parent rendering
time by **2.3527%** in four A/B/B/A replays. It does **not** meet the existing
five-percent threshold for promotion to a new desktop trial. No desktop or
Tahoma gain is claimed.

Both workers use the qualified turbo imports. A is worker v26 with the original
compact program model. B is otherwise identical except for the pinned program
model hash, selecting the [qualified vector-fill kernel](../native-program-vector-fill-component-v1/README.md).
Every other kernel and all host rendering logic remain unchanged.

Worker SHA-256:
`ed339851a1a419cb530e71cf49e9535101f41c9c705eeec60aaa97348759f947`.

## Measurements

Selected correctness and all 972 recorded operations pass. All 3,888 comparison
images match, with one provider generation and no provider failure in each phase.

| Phase | Parent rendering time | Child rendering time |
| --- | ---: | ---: |
| A1 | 101,647.4985 ms | 95,160.4596 ms |
| B1 | 98,686.1529 ms | 92,170.1703 ms |
| B2 | 99,324.7688 ms | 92,797.7503 ms |
| A2 | 101,134.3676 ms | 94,648.9564 ms |

Across 1,944 matched request pairs, parent time falls from 202,781.8661 to
198,010.9217 ms. There are 894 slower individual pairs. Time outside the child
increases from 12,972.4501 to 13,043.0011 ms. These are two full runs per worker,
not independent hardware trials for each request.

The retained route analysis attributes 45,280.2248 ms of baseline child time and
39,483.5593 ms of candidate child time to the program route across both runs:
a 12.80% reduction for the route whose kernel changed. Its 463 requests per replay
are a minority of total execution time. Those child intervals include host work
and NPU waits; this is not an isolated measurement of `memset` instruction cost.

No dedicated worker-v27 recovery or live trial is run. The improvement is retained
as a candidate for a separately measured combination, rather than being treated
as sufficient for desktop promotion by itself.

## Evidence

`artifact-sha256.json` identifies 114 artifacts. Remote source artifacts total
19,092,541 bytes. Large evidence files are compressed losslessly; the manifest
includes both stored hashes and uncompressed source hashes and lengths.
Sources, build, correctness, all four timed replays and route analysis are
retained. Models and executable binaries are excluded.
