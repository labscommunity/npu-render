# Panther Lake NPU capacity query

OpenVINO 2026.4.1 reports Intel AI Boost architecture 5010 with
`NPU_MAX_TILES = 3`. The current experimental models already select three tiles.
Configured tiles do not establish actual parallel utilization.

The query also reports `NPU_TURBO = false`, default tile selection `-1`,
asynchronous request range `[1, 8, 1]`, and stream range `[0, 8]`. This read-only
query compiles or executes no model and changes no setting. Turbo has not been
benchmarked; these properties do not demonstrate faster rendering or a Tahoma
benefit. The [manifest](artifact-sha256.json) retains the probe and device receipt.
