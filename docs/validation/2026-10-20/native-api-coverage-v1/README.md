# Current Windows adapter: static coverage inventory

The source used by driver package v19 initializes 101 D3D10 device-table entries
to explicit rejection, then overrides 80. **Twenty-one entries still reject.**
An overridden callback is not necessarily a supported feature: geometry-shader
binding, stream-output targets and predication accept only null bindings, while
flush/hazard callbacks rely on synchronous rendering.

This inventory verifies source hashes against `native-auto-umd-v2-source.json`.
The package script binds that source's UMD build to package v19, used by the
latest physical trial. The inventory does not run a driver, establish API
conformance, or qualify an application.

`coverage.json` contains the exact callback names and the five explicitly
rejected DXGI callbacks. The source and the small audit script are included.
The ten artifacts are identified by `artifact-sha256.json`.
The actionable feature boundaries are summarized in
[Windows graphics coverage](../../../research/windows-graphics-coverage.md).
