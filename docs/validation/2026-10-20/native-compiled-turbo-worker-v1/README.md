# Compiled-graph turbo: complete renderer comparison

Worker30 preserves all 3,888 A/B/B/A images and reduces aggregate parent
rendering time by **3.1253%** against worker26. Both injected provider-recovery
scenarios pass. The improvement is below the five-percent live promotion
threshold; this candidate has no new physical desktop or Tahoma result.

The only provider-source change requests and verifies `NPU_TURBO=true` when
compiling NPU graphs. Worker sources, custom model blobs and shader arithmetic
are unchanged. The built worker SHA-256 is
`d83d6b1223a6ea815b907e3168e099dc5be614633010f7c11ddd090b15291a80`.
Both builds use the same MSVC, SDK, CMake generator and release flags.

## Correctness and timing

All 174 selected and 972 complete qualification images pass. Each timed phase
contains 972 exact images, one provider generation and no provider failure.

| Phase | Worker | Parent rendering time | Child rendering time |
| --- | ---: | ---: | ---: |
| A1 | 26 | 117,608.6159 ms | 105,251.2573 ms |
| B1 | 30 | 114,473.4755 ms | 102,306.6511 ms |
| B2 | 30 | 114,070.0679 ms | 101,856.2836 ms |
| A2 | 26 | 118,308.1397 ms | 105,827.2443 ms |

Across 1,944 matched request pairs, parent time falls from 235,916.7556 to
228,543.5434 ms. There are 526 slower individual pairs. These are two runs per
worker, not 1,944 independent experiments. Timings include host orchestration,
transport and NPU waits; they are not pure kernel timings or measured clocks.

First-use compilation is separate from this warmed comparison. The full
qualification records 216,453.346 ms in backend compilation, with 44 disk-cache
hits across 131 backend calls. Its preparation and drawing counters are nested
and must not be added together. Compile-time cost is not a desktop frame rate.

## Recovery and provenance

Two thirty-image tests inject a provider exception and terminate the owned
child, respectively. Each retries original ticket 587 on NPU, returns thirty
exact images and records two generations, one failure and one retry.
Compiled-graph turbo is read back successfully in both generations: twelve
verifications in the first and eight in the second. These are bounded injected
faults, not a test of real device loss.

The [first build-wrapper rejection](../native-compiled-turbo-build-rejected-v1/README.md)
is retained. Its libraries and two CPU tests had passed, but the wrapper expected
a different CTest summary string. V2 pins the existing build artifacts, reruns
the same two tests and validates their structured JUnit result. It does not
replace the failed report or rebuild different libraries.

Both timed workers run after Windows servicing, under the same one-off local
task context. The machine remains on boot `2026-10-20T11:02:28.5000000Z`;
Windows is 26200.9711, with NPU driver 32.0.100.5540 and GPU driver
32.0.101.8724. Package power and actual frequencies were not measured.

`artifact-sha256.json` identifies 442 artifacts, including provider and worker
sources, build provenance, qualification, comparison, recovery, preparation
accounting and route attribution. Remote source artifacts total 21,764,241
bytes; large files are compressed losslessly with stored and source hashes.
The original failed build evidence remains in its separate archive.
