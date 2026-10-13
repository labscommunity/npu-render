# Compact coverage runtime experiment

The opt-in NPU coverage implementation passed all 1,438 saved desktop requests
with exact pixel and coverage results. It does **not** establish a desktop
performance improvement: this replay took 657.860 seconds, compared with
631.781 seconds in the earlier point-reentry run. Run order and warming were
uncontrolled. The implementation remains an isolated experiment.

The source patch applies to the preserved `point-reentry-v1` experiment. It
selects compact coverage only for NPU batches that satisfy the exact numerical
admission rule documented in [the arithmetic experiment](../coverage-probes-v1/README.md).
Other batches run the existing wide coverage graph on the same device.
Provider failures propagate; there is no CPU coverage fallback. CPU and GPU
continue to select the original wide graph. Standalone compact GPU execution
failed in the earlier experiment and is not enabled here.

Validation included a clean Windows build, all 29 CPU contract suites, explicit
NPU raster/uniform/triangle-texture tests, 55 replay-comparison boundary cases,
and the complete saved v120 desktop trace. The replay baseline is an earlier
NPU result, not an independent CPU oracle. The isolated arithmetic and rendering
contracts supply independent expected results within their tested domains.

| Measurement | Earlier point-reentry replay | Compact coverage replay |
| --- | ---: | ---: |
| Total replay time | 631.781 s | 657.860 s |
| Summed draw time | 288.775 s | 304.643 s |
| Summed setup plus draw time | 470.259 s | 446.907 s |
| Provider calls | 57,221 | 57,221 |
| Model setup calls | 1,730 | 1,775 |
| Disk cache hits | 1,730 | 1,770 |

Compact arithmetic handled 13,992 coverage calls and 51,063,808 samples across
1,102 requests. The wide path handled 2,767 calls; 228 requests used both paths.
Every provider-call count, coverage count, output-byte count, tile count, and
pixel digest matched. The only admitted receipt change was exactly eight extra
input bytes per compact sample, independently accounted for by worker telemetry:
408,510,464 additional logical input bytes. Total logical inputs were
89,775,142,980 bytes; these are renderer API counts, not measured DRAM traffic.

The two-second process sampler observed peaks of 1,792,028,672 working-set bytes
and 680,017,920 private bytes. It recorded approximately 306.422 worker CPU
seconds over its sampled interval. These are process measurements, not NPU
memory allocation or total machine energy measurements.

The replay harness rewrites a growing JSON report around every request. It also
starts its per-request timer after publishing the request and writing the
report. Consequently, those roundtrip values omit work, and 197 were zero.
Its Windows monotonic clock is coarse. Worker draw timing is separate; setup
plus draw uses `GetTickCount64` and should be assessed in aggregate. A matched
same-binary off/on/on/off trial with reduced report writing and a high-resolution
controller timer is being recorded separately. Historical measurements remain
unchanged.

Fifteen raw artifacts were checked against remote SHA-256 and size receipts
before normalization for this archive. The full replay report preserves every
request digest and measured receipt; multi-gigabyte raw packets remain in the
private host archive. Source hashes were checked against the clean build report.
The initial hardware qualification report predates the full replay and retains
its historical `full_desktop_replay_passed: false`; the subsequent replay report
records the completed result.

No live desktop, physical display migration, or Tahoma workload was tested in
this experiment. The production renderer was not changed.
