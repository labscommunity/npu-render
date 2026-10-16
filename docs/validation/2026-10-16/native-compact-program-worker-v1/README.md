# Compact program worker replay

Worker v17 passes 106 selected requests and all 972 retained requests. Every
output from worker v15 is preserved: 957 original capture matches, fourteen
previously qualified profile matches and one request without a captured image.
No new numerical exception is introduced. One provider generation completes
without failure, with 1,944 correlated parent/child timing records.

The replay exercises 463 generic-program draws with larger batches, 126 geometry
draws, ten complex-shader draws, six long-shader draws and 37 rectangles. The
rectangle policy remains 28 direct kernels and nine retained NPU crops. The
program model is the separately qualified compact ABI-three kernel.

The first integration, worker v16, stops at request one because the address
scheduler retains its 16,384-sample budget limit. No larger-batch rendering
result is accepted. Worker v17 keeps that scheduler budget capped while the
provider separately validates its larger transport batches. Region addresses
and blend ordering are unchanged. Its first replay renders all 106 requests
correctly, but the audit still enforces the old 12,288-sample transport limit.
The updated audit uses the kernel's explicit maximum, requires larger batches
to be observed, and reruns both selected and full replays with the identical
worker binary. Both failures remain in this archive.

The archive pins 135 source and evidence files. It excludes private pixels,
model blobs, caches and compiled binaries. This qualifies recorded-output
preservation, not a speedup. Paired complete-image comparisons, registered
graphics and live desktop responsiveness remain separate gates. Full desktop
offload, GPU relief and a Tahoma benefit remain unqualified.
