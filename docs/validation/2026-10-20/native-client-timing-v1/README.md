# Native client timing: rendering remains dominant

The instrumented UMD uses the unchanged worker19 renderer. Its build, admission,
trace lifecycle, driver staging and registered Direct3D checks pass. The finite
desktop trial restores topology, ordinary windows and memory integrity, but
**fails its overall coverage check**: no texture-specialization acceptance is
observed. This diagnostic must not replace the preceding passing desktop trial.

During the run, the renderer records 1,489 hardware-child receipts with one
generation and no provider fault/retry. The presenter verifies 92 exact
submissions and 83 distinct source hashes. All forty repeated inputs receive
matching display-counter observations. Animation remains seven verified updates
in ten seconds, with nine including drain. Warm sent-input-to-display p95 is
108.996 ms for mouse and 3,246.747 ms for keyboard; each warm set has nineteen
observations. This is an instrumented diagnostic, not a matched performance A/B.
The initial default-device probe fails before application admission. The later
normally started, non-admin shared-worker fixture passes eight NPU operations
with exact parent/child receipt and acknowledgment correlation. General
application migration remains unqualified. The retained DWM trace also contains
one final rejected draw and capture; its relationship to finite-run shutdown has
not been established, and it is not counted as a successful operation.

[Client timing analysis](raw/native-client-timing-v3-analysis/report.json)
unambiguously joins all 1,482 measured DWM completions to their worker requests.
There are 1,490 completed parent exchanges, plus one final incomplete request,
which is retained explicitly and excluded from completed-request timing.

| Measured client stage | Total across 1,482 requests | Median | p95 |
| --- | ---: | ---: | ---: |
| Request encoding, including nonce | 2,791.891 ms | 0.329 ms | 10.454 ms |
| Explicit admission verification | 314.699 ms | 0.200 ms | 0.301 ms |
| Exchange, including validation and acknowledgment | 254,272.738 ms | 22.473 ms | 302.749 ms |
| Second response decode | 917.334 ms | 0.209 ms | 4.444 ms |
| Parent execution, contained within exchange | 246,979.562 ms | 19.926 ms | 291.851 ms |

The four sequential client stages total 258,296.661 ms; about 95.6% is inside
parent rendering execution. That interval includes CPU orchestration, child
transport and NPU execution/waits. It is not NPU hardware utilization.
Diagnostic trace calls cumulatively take 1,521.439 ms over the two observed
counter epochs, including 1,460.404 ms in their admission checks. These nested
counters overlap and must not be added to the table as an exclusive partition.

The [ten-second animation analysis](raw/native-animation-v3-analysis/report.json)
independently places 6,876.278 ms inside child rendering, 463.700 ms before child
connection, 378.875 ms reading the child packet, 1.201 ms before child execution,
415.121 ms after it, and 1,864.826 ms outside parent execution. It verifies seven
publications and submissions. The clipping shader targeted by the new combined
kernel accounts for 35 overlapping draws and 1,513.463 ms of child time. The
five-sample shader accounts for 1,511.265 ms; the rectangle/quad shader accounts
for 1,434.123 ms. None of these overlaps implies an achieved optimization.

Two failed analysis attempts are preserved. The first incorrectly required an
identity and completed timeline for the final outstanding request. The second
incorrectly ordered the server's post-write timestamp before client reply receipt:
the client can read the pipe before the writer records that timestamp. The
corrected join contains the parent rendering interval inside the client exchange
and still rejects every missing or ambiguous measured completion. It partitions
diagnostic counters by process-header epochs because DLL reloads reset them.
No timing tolerance or unmatched completed sample is silently discarded.

The 190-file manifest retains the exact instrumented sources, build and driver
identities, failed live gate, successful cleanup, raw traces, all analysis
attempts and preparation scripts. Models and compiled binaries are not committed.
The follow-up timing analyses ran on the completed traces after the controller
stopped; they do not turn its failed live result into a pass.

The next priority is reducing rendering work and NPU launches, while retaining
transport measurements for subsequent batching work. This run establishes no
GPU-resource relief, full desktop offload or Tahoma improvement.
