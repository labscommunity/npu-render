# Correlated request timing in the native NPU worker

Worker v7 changes only `local_indexed_worker.cpp` from v6's nineteen-file source
set. It records each request's QPC arrival, queue entry, execution, reply and
acknowledgment times, together with pixel-program SHA-256 and request identity.
Rendering math, provider configuration and model selection are unchanged.

The 44-request selection and all 972 recorded requests pass. An independent audit
matches all 972 pixel outputs to worker v6. The original reference contract stays
962 original matches, nine qualified precision corrections and one request with
no original reference. No further reference corrections are introduced.

All 1,944 parent/child timeline records have ordered timestamps, matching shader
identities and correlated parent/child request sequences. Their QPC frequency is
10,000,000 Hz. Shared geometry admits 419 draws and the masked path 41, each with
one model import. Four-sample, solid and three-texture admissions remain 24, five
and nine. There is one provider generation and no provider faults.

Registered Direct3D v30 passes eight real NPU operations and restores the ordinary
adapter state with memory integrity enabled. The worker binary SHA-256 is
`152179ba36ac436e5a9b42ae5fa40ae89dd9989d68b8aa04b32f060f3309f31a`.

These checks qualify pixel preservation and timing correlation. The full replay
ran while a separate CPU-only driver candidate was compiling; replay elapsed
times are not a matched performance benchmark. Worker intervals include CPU
orchestration and NPU waits, not hardware utilization. Live input attribution,
responsive desktop use, automatic application rendering and Tahoma benefit are
separate gates.
