# Turbo hint: complete renderer replay and bounded recovery

Worker v26 preserves every recorded output and reduces aggregate parent rendering
time by **9.9033%** in four complete A/B/B/A replays. This qualifies a candidate
for a separate desktop trial. It does **not** measure desktop responsiveness,
GPU relief, power consumption or Tahoma performance.

The only renderer changes from worker v24 introduce a checked `NPU_TURBO=true`
property at ten custom-model import sites. All model blobs and shader arithmetic
remain unchanged. The compiled vertex and provider libraries remain unchanged;
this does not enable the hint for every compiled graph. Property readback is
verified on import. No global Windows power policy is changed.

Worker SHA-256:
`6e28d3cf151450ef802a85d0b01b81b36d8e9aa13afbfba1c75bd57a13826bd0`.

## Correctness and comparison

The selected replay and all 972 recorded operations pass the existing output,
route, identity and parent/child timeline checks. The full A/B/B/A comparison
preserves all **3,888** images, with one provider generation and zero provider
failures in each phase. A is worker v24; B is worker v26.

| Phase | Parent rendering time | Child rendering time |
| --- | ---: | ---: |
| A1 | 107,188.8120 ms | 100,530.5561 ms |
| B1 | 96,913.8152 ms | 90,324.9634 ms |
| B2 | 96,442.8204 ms | 89,928.4041 ms |
| A2 | 107,421.3586 ms | 100,730.8902 ms |

Across 1,944 matched request pairs, parent time falls from 214,610.1706 to
193,356.6356 ms. There are 380 slower individual pairs. Time outside the child
falls only from 13,348.7243 to 13,103.2681 ms. These are two complete runs per
worker, not independent samples for every operation.

The retained route analysis joins every sequential child request to its shader
identity and accepted route. Program-route child time falls from 56,904.2017 to
44,461.8782 ms across the two runs. Other NPU routes that lack an accepted custom
route marker total 74,373.3887 ms in B, about 41.3% of child time; their aggregate
changes little. The largest individual shader group comprises sixteen requests
per replay and 31,752.5118 ms across B1/B2. These intervals include host work and
NPU waits; they are not pure kernel timings. The analysis identifies further
work rather than proving a remedy.

## Recovery and retained failure

Both thirty-image recovery cases pass: an injected provider exception and
termination of the owned child process. Each uses two generations, one observed
failure and one NPU retry, preserving all thirty outputs and acknowledgments.
The retry targets original ticket 587. Six checked turbo imports appear in each
generation in these cases. They test bounded process/provider recovery, **not**
a new real NPU device-loss event.

The first worker v25 attempt fails compilation because an import helper was
unqualified across C++ namespaces. Its build failure and source remain archived.
Worker v26 fixes those ten call sites; v25 never executes on the NPU.

## Evidence

`artifact-sha256.json` identifies 193 retained artifacts. Remote originals total
19,699,867 bytes before the additional local analysis. Larger files use lossless
gzip compression: the manifest records both stored SHA-256 and the uncompressed
source SHA-256/length. For example, use `gzip -dc` to inspect a `.json.gz` or
`.jsonl.gz` file. Model blobs, binaries and credentials are excluded.

The performance hint is subject to platform power limits. Its effect on a
concurrent GPU workload must be measured separately; Intel's documented hint
semantics do not establish a Tahoma gain. See the
[OpenVINO NPU properties documentation](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html#supported-features-and-properties).
