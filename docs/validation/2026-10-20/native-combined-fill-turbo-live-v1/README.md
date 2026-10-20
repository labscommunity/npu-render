# Combined vector-fill/turbo renderer: failed desktop qualification

Worker31's physical trial completes and restores the machine, but **does not
pass desktop qualification**. The required texture specialization is never
observed. Its shader is absent from this run's requests; the prior passing run
contained two requests using it. The failure remains recorded and the check
remains required.

Diagnostic timing is also inadequate: **0.7 verified animation updates/s**,
with warm sent-to-display medians around 129 ms and maximums over one second.
The preceding passing worker26 run remains the baseline at 0.9 updates/s.
These are separate runs with different desktop request mixes, not a matched
measurement of the effect of worker31's changes.

## Completed work and restoration

The readiness task detects sign-in and starts coordinator v17 at 13:10:46 UTC.
Registration v54 passes and cleans up. Admission v75 passes actual DWM/NPU
execution and ordinary recovery; the outer live v65 check fails on route
coverage. The readiness task finishes at 13:23:28 UTC and has no recurring trigger.

- 1,464 acknowledged child operations; one provider generation, zero provider
  faults and retries.
- 111 physical submissions exactly match their source frames, with 90 distinct
  source pixel hashes over the 550-second presentation interval.
- All forty repeated inputs have matching physical observations. Pointer
  confinement/release, primary topology and ordinary windows are restored.
- Nine custom-model turbo readbacks and 122 compiled-model turbo verifications
  pass in the single generation.
- Native cleanup passes; memory integrity remains enabled.

The unleased default-device observer still fails creation with `0x887a0004`.
The coverage exception occurs before the later shared-application qualification
checks. Those checks must not be inferred to have passed from other receipts.
The physical bridge still uses GPU copying, presentation and observer readbacks.
Residual GPU cost and Tahoma performance are unmeasured.

## Diagnostic timing

The normal coordinator skips its timing-analysis stage after the live failure.
A separately recorded read-only postmortem runs the same pinned analyzers after
completion and verified restoration. Their successful analysis does not change
the failed physical qualification.

| Warm input, 19 observations each | Median sent-to-matching-display | p95 / maximum |
| --- | ---: | ---: |
| Mouse | 129.2337 ms | 1,222.5864 ms |
| Keyboard | 128.5537 ms | 1,566.9755 ms |

Warm excludes the first event of each kind; with nineteen samples p95 equals
maximum. The first mouse takes 766.4051 ms, including 646.9952 ms before the
application receives it. Two later outliers instead occur mainly after receipt:
keyboard sequence 8 takes 1,534.6307 ms to publish; mouse sequence 39 takes
1,190.3725 ms. Their child-render overlap is 734.2801 and 1,084.6155 ms,
respectively. This locates delays without proving a power-related cause.

The producer requests all 300 changes at 30 Hz with zero skipped updates.
Seven unique colors reach matching display statistics within ten seconds; nine
including drain. The other 291 requested colors have no matching submission.

| Ten-second animation interval | Time |
| --- | ---: |
| Parent work before child connection | 855.8178 ms |
| Child packet read | 843.2711 ms |
| Child work before execution | 3.6403 ms |
| Child rendering execution | 5,130.8525 ms |
| Parent work after execution | 859.3213 ms |
| Outside serialized parent execution | 2,307.0970 ms |

The 205 overlapping child requests contain 2,870 whole-request NPU calls and
zero backend compilation time. These counters are not apportioned for requests
crossing the interval boundaries. One complex shader contributes 1,613.9788 ms
of child overlap. Host work and NPU waits both contribute to child time.

## Evidence and next boundary

`artifact-sha256.json` pins 180 artifacts. The collector verifies 7,005,162 bytes
of remote evidence and stores large files with lossless gzip compression.
Local timing and route-coverage analyses retain their source hashes.

The absent shader explains the missing natural-workload coverage, not the
responsiveness failure. A future qualification needs a reproducible coverage
workload while keeping route checks intact. Batching real draw dependencies,
reducing rendering calls, supporting ordinary applications, and measuring GPU
relief with Tahoma remain separate unfulfilled requirements.
