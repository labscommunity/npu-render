# Combined four-sample and solid-color native worker

The combined worker passes all 972 recorded requests with one provider generation
and zero provider faults. It admits all 24 observed four-sample draws and five
solid rectangle groups. Another 28 attempted solid geometries are explicitly
declined after NPU vertex/projection checks and use the existing NPU renderer;
their actual extra operations are included in the receipt. No CPU/GPU fallback
is enabled.

All 971 available original pixel references match. A separate comparison against
the prior qualified worker matches all 972 output hashes and byte lengths,
including the request without an original response. The selected replay also
passes all 19 requests, with ten four-sample and five solid admissions.

Registered Direct3D qualification v26 passes and removes its temporary adapter.
The driver, executable and both NPU models are pinned. Recovery passes and memory
integrity remains enabled; no boot setting is changed. Source snapshots, receipts,
logs, failure counts and the independent hash comparison are retained.

This qualifies replay integration and the finite registered Direct3D test. The
live desktop trial with this combined worker is recorded separately. Responsive
desktop use, normal application migration, GPU relief and Tahoma benefit remain
unproven. Compiled models, executables and private captured pixels are excluded.
