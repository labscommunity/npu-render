# Response validation without a pixel copy

The candidate response-envelope validator passes 3,598 equivalence checks against
the qualified full decoder on Delta. Tests cover header and receipt bit changes,
request mutations, truncated and oversized packets, failure packets, and four
valid surface sizes. Accepted receipt fields and rejection categories match.

The candidate keeps the decoder's validation prefix and returns its receipt
without allocating and copying a full pixel vector. The original validation
checks exact payload extent but does not inspect individual pixel values.
This component performs no NPU inference and does not render a desktop.

Across 39 alternating warm pairs per surface, total validation time is:

| Surface | Full decoder | Envelope validator |
| --- | ---: | ---: |
| 640 × 480 | 11.8007 ms | 0.0027 ms |
| 1920 × 1080 | 69.5143 ms | 0.0031 ms |
| 2880 × 1800 | 182.7634 ms | 0.0061 ms |
| 4096 × 2047 | 298.1828 ms | 0.0061 ms |

These narrowly scoped measurements exclude transport, rendering and presentation.
They establish a candidate copy removal, not an end-to-end speedup. Worker replay,
fault recovery, paired worker timing and live desktop qualification are separate
requirements.

The first coordinator remains waiting after its build child exits. Its owned
process is identified and terminated with no active graphics process; the
recovery record and failed coordinator are retained. The second coordinator
verifies the completed build's sources and binary, then runs the comparison
with a bounded process wait. The manifest pins twenty source and evidence files.
