# Registered-adapter probe: rejection controls

These captures are from the Panther Lake Windows machine. No usable NPU Render
adapter was registered, and no desktop or Tahoma work moved in these tests.

The new `nr_registered_probe` selects an explicit DXGI adapter LUID and queries
its kernel identity before attempting normal `D3D11CreateDevice` creation.
The intended creation call supplies that adapter, `D3D_DRIVER_TYPE_UNKNOWN`,
and a null software-driver module. It never retries on the default adapter or
WARP. The specified UMD is preloaded only to configure its compute backend and
read module-specific execution statistics.

Captured controls:

- Absent adapter: `DXGI_ERROR_NOT_FOUND`, no device creation attempted.
- Intel Arc adapter: rejected as a foreign kernel adapter before creation.
- Microsoft software adapter: rejected as a foreign kernel adapter before
  creation. Neither rejection submitted rendering or inference work.
- Six relevant Windows CTests passed, including malformed-LUID and actual
  software-adapter rejection, normal UMD callback contracts, and the existing
  software DDI CPU rendering/rejection controls.

`registered-adapters.json` records the enumerated adapter identifiers.
`source-sha256.json` binds the probe build inputs; `captured-sha256.json` hashes
the captured reports and compressed original test log. The LUIDs identify this
Windows boot and are not stable installation identifiers.

The positive registered-adapter path is unvalidated. The paired kernel driver
still rejects startup; its readiness flag is zero. These controls establish
that the probe cannot report a successful migration merely because another
renderer created a device. They do not establish NPU graphics conformance,
DWM selection, GPU work retirement, or a Tahoma performance improvement.
