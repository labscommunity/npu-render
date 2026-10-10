# Initial indirect-display startup tests

The first diagnostic fails with Code 31. The revised callback/trace build passes
framework initialization and fails at adapter discovery with Code 10. Neither
reaches desktop adapter selection. Both packages are removed cleanly and the
original display and paired render adapter retain their status.

[report.json](report.json) preserves exact errors, build provenance, startup
traces, failed hypotheses and the next enumeration check. These are startup
diagnostics, not desktop offload results.
