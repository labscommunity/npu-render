# Combined clipping worker: exact images, targeted gain

Worker23 integrates the [device-qualified clipping kernel](../native-resident-clip-component-v1/README.md)
into the isolated NPU renderer. The selected replay and full 972-request replay
pass, preserving every worker19 output. All 63 admitted clipping draws execute
the new route, with one shared model import and no normal-run provider failure.

Four full A/B/B/A replays preserve all 3,888 images:

| Phase | Worker | Parent execution | Child execution | Replay elapsed |
| --- | ---: | ---: | ---: | ---: |
| A1 | 19 | 120,223.825 ms | 113,397.176 ms | 207.325 s |
| B1 | 23 | 115,167.968 ms | 108,478.003 ms | 201.312 s |
| B2 | 23 | 114,779.757 ms | 108,120.476 ms | 200.726 s |
| A2 | 19 | 120,528.655 ms | 113,662.863 ms | 209.239 s |

Across 1,944 pairs, parent execution falls from 240,752.479 to 229,947.725 ms,
an aggregate **4.49% reduction**; 895 individual pairs are slower. This is below
the existing five-percent desktop promotion threshold.

[Targeted attribution](analysis/targeted-cost.json) selects the 63 actual routed
packet tickets, joining original tickets through replay iteration rather than
potentially repeated client sequence numbers. Their 126 pairs fall from
13,029.346 to 3,689.122 ms, **71.69% faster**, with no slower targeted pair.
Complete-draw NPU calls across the 63-request correctness replay fall from 1,484
to 756. These counts include vertex/projection work and all raster batches, not
just the combined kernel. The targeted gain is not a whole-desktop gain.

Two recovery cases also pass: injected provider exception and owned-child
termination. Each preserves twenty images and acknowledgments, with two provider
generations, one failure and one retry on NPU. Failure is at dispatch 8, original
ticket 100, after ticket 97 has initialized and used the new provider. Each case
observes seven clipping draws and two model imports. Actual hardware device loss
is not induced.

The worker SHA-256 is
`b00ce5d698757721f9fa18947430b328fb3278886d2127cfbd9cfee4620142f4`.
The 140-file manifest retains the source bundle, build identities, selected/full
replays, complete comparison, recovery cases and analysis. Model binaries and
compiled artifacts are excluded.

The [standalone live gate](../native-resident-clip-live-v1/README.md) rejects this
candidate before changing the desktop. Combining it with the separately checked
geometry and rectangle changes is a new experiment, requiring its own exact
replay, comparison, recovery and desktop qualification. Full desktop offload,
GPU-resource relief and a Tahoma benefit remain unproven.
