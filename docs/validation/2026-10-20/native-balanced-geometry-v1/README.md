# Balanced geometry packing: small component improvement

Distributing geometry records evenly across the six existing kernel spans
preserves all 972 captured images and all 3,888 images in four A/B/B/A replays.
Aggregate parent request time falls from 229.182 to 224.825 seconds, **1.90%**,
with 908 slower individual pairs among 1,944 comparisons.

| Phase | Worker | Parent request time | Child request time | Replay wall time |
| --- | --- | ---: | ---: | ---: |
| A1 | 19, fixed packing | 114.797 s | 108.053 s | 204.974 s |
| B1 | 21, balanced packing | 112.836 s | 106.079 s | 203.748 s |
| B2 | 21, balanced packing | 111.989 s | 105.240 s | 201.204 s |
| A2 | 19, fixed packing | 114.385 s | 107.603 s | 202.724 s |

This observed improvement falls below the five-percent live promotion threshold.
No new desktop, provider-fault or Tahoma trial was run for this candidate.
Two runs per worker do not establish a general performance guarantee.

Only `ptl_geometry.hpp` changes. A 4,096-record batch previously filled spans with
3,072 and 1,024 records, leaving four empty; the candidate distributes the records
evenly. Kernel arithmetic, model bytes and linked provider libraries remain
unchanged. The recorded schedule proves logical partitioning, not physical NPU
core utilization. Every complete replay uses one provider generation without
failure, and all worker-19 output hashes are preserved.

The [manifest](artifact-sha256.json) covers 105 artifacts: 103 checked against
remote SHA-256 receipts and two local preparation/collection sources. Reports,
worker sources, build identity, complete request traces and the comparison's
individual pairs are retained.
