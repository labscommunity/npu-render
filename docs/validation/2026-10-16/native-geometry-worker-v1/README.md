# Shared geometry in the native worker

The worker now selects the qualified NPU geometry component for eligible draws
across shader groups. It retains the existing pipeline cache and explicitly
reports unsupported geometry using the original NPU path. A selected custom
provider failure remains an error. No CPU rendering fallback is introduced.

The selected 44-request check passes. The full 972-request replay also passes:
962 original references, the same nine independently qualified precision
corrections, and one request without an original reference. A separate audit
matches all 972 outputs against worker v4. This revision introduces no additional
pixel corrections.

The full replay admits 460 shared geometry draws and declines 24. It imports the
geometry model once, including its use by the three-texture specialization. The
other paths admit 24 four-sample draws, five solid draws and nine three-texture
draws. There is one worker provider generation and no provider failure.

Registered Direct3D v28 passes and restores the machine with memory integrity
enabled. Worker binary SHA-256:
`4f568d542b73e17750b0369e5b9f1e34a26ef64509b86db54f7569efd153684a`.
Geometry model SHA-256:
`ea4432abd7b548396b86665c1b318353e0d2f3ec847bcd3e6f56c10bd1336dfa`.

This is replay correctness and registered-device qualification. The replay is
not an isolated performance benchmark. Live interaction, sustained operation,
automatic application admission and a Tahoma benefit are separate gates.
The manifest retains source and reports byte for byte; executable binaries,
model blobs and pixel images are excluded.
