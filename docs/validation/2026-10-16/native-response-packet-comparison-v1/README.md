# Response-packet worker comparison

Four complete replays run in A/B/B/A order: worker v18, v19, v19, v18.
Every run preserves all 972 expected images, acknowledges all deliveries, and
records one provider generation with no fault. Both workers use the same kernels.
The changed worker forwards a validated response packet without decoding and
re-encoding its full pixel payload in the parent.

| Run | Worker | Replay elapsed | Parent request total | Child execution total | Parent time outside child |
| --- | --- | --- | --- | --- | --- |
| A1 | v18 | 203.329 s | 121.706 s | 112.777 s | 8.929 s |
| B1 | v19 | 195.915 s | 116.912 s | 110.120 s | 6.792 s |
| B2 | v19 | 203.801 s | 121.381 s | 114.586 s | 6.794 s |
| A2 | v18 | 204.492 s | 121.518 s | 112.636 s | 8.883 s |

Across 1,944 paired requests, parent request time falls from 243.224 to
238.292 seconds (**2.03%**). Time outside the child falls from 17.812 to
13.586 seconds (**23.73%**). There are 610 slower individual parent pairs.
The outside-child interval includes IPC and scheduling as well as packet work;
it is not an isolated measurement of copying. Replay elapsed time also includes
client work, startup and file I/O.

The result qualifies exact prior-output preservation and a small aggregate
request-time improvement. It does not establish desktop responsiveness, GPU
relief, NPU utilization, power savings or a Tahoma benefit. No desktop topology
change or concurrent Tahoma run occurred during this comparison.

The [manifest](artifact-sha256.json) contains 44 artifacts, including all four
replays, parent/child timelines, the complete 1,944-pair table, coordinator and
comparison sources, and both build identities. Remote files were verified against
Delta's SHA-256 receipts before curation.
