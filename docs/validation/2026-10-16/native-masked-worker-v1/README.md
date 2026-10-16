# Masked-texture path in the native worker

Worker v6 adds the qualified two-phase masked-texture component to the existing
triangle cache. Selection requires the exact shader, supported resources,
bounded geometry and source/source-over blending. Other draws retain their
qualified NPU paths. A selected component failure remains an error.

The selected 44-request replay passes, including 13 masked draws. The full replay
passes all 972 requests: 962 original references, the unchanged nine qualified
precision corrections, and one request without an original reference. An
independent audit matches all 972 output hashes against worker v5. This change
introduces no further pixel corrections.

The full replay admits 41 masked draws and 419 other shared geometry draws. Both
models import once. The previous specializations admit 24 four-sample draws,
five solid draws and nine three-texture draws. One provider generation completes
with no failures. The 24 geometry declines explicitly retain the original NPU
path. Registered Direct3D v29 and recovery pass with memory integrity enabled.

Worker binary SHA-256:
`315b27d70a55150bc52ee7a59fcf4b553ca0dec23743c8b23536ec3d99501342`.
Masked model SHA-256:
`81104c9ea8760bbeb7cc028ea2a9c81e8605482af219f57c082c737b9662d1e0`.

This establishes worker correctness and registered-device operation. It is not
a matched whole-worker performance benchmark, live desktop qualification or
Tahoma benefit. The manifest preserves source and reports byte for byte;
executables, model blobs and private pixels are excluded.
