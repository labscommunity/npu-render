# Host staging rejected after complete rendering comparison

Binding ordinary cacheable host input/output tensors makes this renderer slower.
The candidate preserves all 972 captured images, passes both bounded provider
recovery cases, and preserves all 3,888 images in four A/B/B/A replay runs.
Across 1,944 paired requests, parent request time increases from 228.955 to
246.132 seconds, **7.50% worse**, with 1,453 slower candidate pairs.

| Phase | Worker | Parent request time | Child request time | Replay wall time |
| --- | --- | ---: | ---: | ---: |
| A1 | 19, plugin allocations | 114.332 s | 107.654 s | 202.833 s |
| B1 | 20, ordinary host tensors | 123.045 s | 116.333 s | 211.801 s |
| B2 | 20, ordinary host tensors | 123.087 s | 116.385 s | 212.580 s |
| A2 | 19, plugin allocations | 114.623 s | 107.943 s | 203.952 s |

The comparison's `passed` means its execution, image and attribution checks pass;
it does not mean the candidate is faster. The separate live coordinator rejects
promotion because the required five-percent improvement is absent. It stops
before registration or desktop changes. No live desktop or Tahoma benefit is
claimed. Two runs per worker establish this observed regression, not a universal
performance bound.

Worker 20 changes only the program, complex-shader and rectangle adapters plus
one host-tensor helper. Model bytes, NPU kernels and linked provider libraries
are unchanged. The helper verifies tensor type, shape, continuity, pointer
identity and Windows memory protection. Binding changes input protection from
write-combined `0x404` to cacheable read/write `0x4`; output remains `0x4`.
OpenVINO manages the transfer. The renderer does not calculate shaders on CPU or
reuse cached results. The preceding [CPU memory probe](../native-tensor-memory-v1/README.md)
explains the expensive diagnostic hashing but did not predict a rendering gain.

The injected provider exception and owned-child termination each replay twenty
requests with exact images, two provider generations, one failure and one NPU
retry. These are controlled process failures, not hardware device-loss tests.
All normal full replays use one provider generation with no failure.

The [manifest](artifact-sha256.json) covers 153 artifacts, including 149 checked
against remote hashes, the candidate source, build reports, comparison and
recovery logs, and the rejected live coordinator. The remaining four are local
preparation/collection sources. Unexecuted live scripts are retained as prepared
inputs; their presence does not establish live qualification.
