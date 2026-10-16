# Compact generic-program payload

The ABI-three kernel passes 448 independent fixtures on Delta's NPU: 384 existing
scalar FP32 oracle cases, 24 larger-batch cases, and 40 invalid contracts. Eight
original-carrier checks pass before and after the custom kernel. Each request
checks all six independent compiler-owned spans, including unused output bytes.
The kernel occupies 18,048 bytes of the existing 18,256-byte code allocation.
This is component qualification; worker integration and desktop performance are
separate gates.

The preceding profiling worker changes logging only. It passes 106 selected
recorded requests with all required pixel checks, 26 generic-program draws,
one provider generation and no provider failure. Its 586 batches account for
4,626.834 ms: 2,370.291 ms waiting for geometry requests, 1,943.188 ms waiting for
color requests, and 313.355 ms in other batch work. Request waits include runtime
and driver overhead; these numbers do not measure NPU utilization or whole-frame
latency. The first analysis script omitted the replay directory in its log path;
the corrected script analyzes the unchanged completed replay.

The old payload reserves 112 bytes per pixel for every program. The new layout
derives color-input liveness from validated shader instructions and carries only
those varyings, addressed texture samples, and required filtering fractions.
Point sampling carries one texel; linear sampling retains four texels and the
same fractions. Geometry tasks follow the bounded vertex table. Independent
input, output and geometry bounds determine batch capacity. The NPU retains
coverage, interpolation, sampling coordinates and color arithmetic. The host
validates addresses and transports data. FP32 expression order is unchanged.

The largest profiled group uses two linear samples and one color varying, for
which the phase-one record shrinks from 112 to 72 bytes. Other programs fit more
samples, up to an explicit limit of 16,384 samples per span. Capacity is not a
speedup measurement: complete-image comparisons must establish correctness and
paired timing before promotion.

The archive pins 85 source and evidence files. It includes the unchanged-kernel
profiling worker, the independent reference fixture transformations, CPU checks,
target build proof, carrier contract proof and hardware receipts. Private pixels,
models, caches and compiled binaries are excluded. No desktop change, GPU-relief
measurement or Tahoma test is performed by this component qualification.
