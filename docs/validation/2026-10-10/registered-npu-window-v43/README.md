# Registered driver → physical NPU → verified window

Three restricted shader draws from the registered VM adapter ran on Delta’s
physical NPU and reached the displayed window. All 552,960 checked RGB bytes
passed the independent screen oracle within one byte; no CPU/GPU draw-provider
fallback or resource leaks occurred.

This is an explicitly remote laboratory path. CPU uploads/copies, authenticated
file transport, and the original desktop compositor/display remain involved.
It does not establish desktop offload or Tahoma improvement. Both earlier failed
attempts, the timeout without fallback, physical-worker negative tests, source
hashes and raw-file manifests are preserved alongside the successful result.
See [report.json](report.json) for exact boundaries and provenance.
