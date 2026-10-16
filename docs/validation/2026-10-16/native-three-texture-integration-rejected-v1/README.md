# Rejected first three-texture integration

The first recorded draw comparison changes 15 of 3,145,728 byte components by one
level. The candidate is rejected; it is not integrated into the native desktop.
The uninstrumented first draw takes approximately 1.51 s versus 2.76 s for the
baseline, but uses 254 calls versus 146. An 8,192-fragment batch needs two groups
of six spans for each of two custom shader stages. These are one-shot component
timings and do not qualify a performance improvement.

A first instrumented replay reproduces the exact same output hashes and byte
mismatches. Its first 8,192-fragment shader batch has no differing float results;
subsequent batches still need diagnosis. Instrumentation contaminates its timings.
Further stage-by-stage diagnostics are separate evidence.

The original build failure is retained: the C++ OpenVINO API requires an explicit
chrono duration for wait_for. The corrected v2 build passes; the failed v1 was
never executed. Both source versions and the failed compiler log remain here.
The copied triangle pipeline is based on the SHA-pinned qualified v45 source,
including its target-size bounds. Existing NPU coverage, interpolation and
blending remain; only the pixel-shader adapter changes.

No desktop state is changed. NPU model and executable hashes are checked before
execution. Recorded pixels and executables remain on Delta and are excluded from
this archive. The 96-fixture component pass does not override this failed draw.
