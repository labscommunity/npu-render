# CPU access to NPU request memory

The corrected read-only diagnostic passes. An imported NPU model's default input
buffer has Windows protection `0x404` (read/write, write-combined); its output
buffer is ordinary read/write `0x4`. No inference executes and no desktop setting
changes. Each measured buffer contains 1.5 MiB of identical deterministic bytes.

| Median CPU operation, eight repetitions | Plugin input | Plugin output | Host allocation |
| --- | --- | --- | --- |
| SHA-256 directly from buffer | 8.827 ms | 0.397 ms | 0.392 ms |
| Copy to host, then SHA-256 | 2.700 ms | 0.442 ms | 0.439 ms |
| Copy to host only | 2.276 ms | 0.045 ms | 0.038 ms |

Order reverses on alternating repetitions. Every copy/hash is checked against
the same expected digest; verification for copy-only timing occurs afterward.
The result explains why fingerprinting input tensors is expensive. It does not
show that normal rendering is slowed by equivalent reads: current renderers
primarily write those input buffers. A candidate binding ordinary host tensors
must therefore pass real rendering and alternating-order timing before promotion.
OpenVINO decides whether to import compatible host storage or use shadow copies;
see its [inference-request memory management](https://github.com/openvinotoolkit/openvino/blob/master/src/plugins/intel_npu/docs/inference-request.md).

The first probe stopped before measurement because the Python execution-device
property was the string `NPU`, while the guard expected a one-element list. The
second explicitly normalizes this representation and still rejects any device
other than `NPU` or `NPU.0`. The failed report and source remain preserved.
The [manifest](artifact-sha256.json) covers thirteen artifacts checked against
remote SHA-256 receipts. This is a CPU memory-access measurement, not proof of
NPU rendering speed, full desktop offload or improved Tahoma throughput.
