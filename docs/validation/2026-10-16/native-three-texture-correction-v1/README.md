# Three-texture draw with a qualified precision correction

All nine retained desktop draws pass four comparisons each against an
algebraically equivalent, precision-preserving NPU graph: 36 comparisons and
253,968,384 exact byte components. Both implementations execute on Delta's NPU.
The unchanged custom kernel uses the original FP32 shader coefficient. The graph
reference moves instruction 112's negation from its input to its immediate,
avoiding the compiler precision loss isolated in the
[coefficient investigation](../native-coefficient-precision-v1/README.md).

Each case retains the legacy output separately and verifies its hash against the
previous qualified worker. The correction changes 337 byte components across the
nine original draws; the legacy captures and references remain unchanged.
Three additional texture/background fixtures per draw also match exactly.
These are explicit reference corrections, not matches against the old pixels.

The v3 build fails on a shadowed variable under warnings-as-errors. V4 fixes the
identifier and builds successfully. Its executable SHA-256 is
`7c9546605ae5578ed30b491068f85340c00e34fb85a6eef70b18a6c7a16b0386`.
The comparison uses alternating baseline-first/custom-first order. Component
times range from 734 to 3,796 ms across these target sizes and fixtures; they are
not frame times or a Tahoma benchmark. Desktop state is unchanged.

The hash manifest retains the source, failed build, successful build and complete
comparison reports. Captured pixels, executable files and model blobs are excluded.
