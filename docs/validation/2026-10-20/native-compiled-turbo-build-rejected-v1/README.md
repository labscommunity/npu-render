# Compiled-graph turbo: build-report rejection

The provider libraries compile successfully, and both existing CPU cache
contracts pass. The first coordinator nevertheless rejects the build because
its text check expects a different CTest summary format:

```text
expected: 100% tests passed, 0 tests failed out of 2
observed: 100% tests passed out of 2
```

The two named tests are `registered_provider_cache_contract` and
`registered_provider_memory_cache_contract`. Their retained log shows both
passing, with zero CTest exit status before the wrapper's separate assertion.
No renderer worker is built or executed by this rejected coordinator, and it
does not reach the NPU correctness, timing, recovery or desktop stages.

The prepared candidate changes only `provider_compile.hpp` relative to the
qualified provider source: request `NPU_TURBO=true` when compiling NPU graphs
and require the compiled-model property to read back true. CPU compilation is
unchanged. Existing custom model imports already request this hint. Cache
identity includes the configured boolean property.

The corrected verification uses CTest's JUnit output, checks the exact two test
identities and success states, and verifies source and build-output hashes.
It retains the successful compiled code and the original failed report.
Those subsequent execution results are separate evidence.

Turbo is an experimental performance setting, not a demonstrated way to free
capacity for Tahoma. OpenVINO documents potential effects on other compute
resources and discourages its use for sustained workloads; concurrent Tahoma
measurements remain necessary.
[OpenVINO NPU properties](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html#supported-features-and-properties).

`artifact-sha256.json` records 288 artifacts with 2,483,699 remote source bytes.
It includes the complete provider source, source manifests, build/test logs and
failed coordinator. SSH rejected the first collection attempt's large inline
inventory command; its source is retained, and the successful collector
uploads and verifies a script instead. No models or executable binaries are
included.
