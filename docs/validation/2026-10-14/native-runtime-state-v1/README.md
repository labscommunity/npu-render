# Native runtime state and opaque-crop investigation

Full desktop offload remains unfinished. No Tahoma benefit is measured here.
All experimental changes remain isolated from the default runtime.

## Native failure and exact replay

Live v4 stopped before driver admission because the nonsecure ASUS screensaver
was active. Its identity was verified and only that screensaver process was
stopped; screen-lock policy and settings were unchanged. Live v5 then completed
165 NPU operations with 165 delivery acknowledgments and submitted one matching
frame to the physical presenter. That frame did not contain the controlled window
color. The NPU subsequently returned `ZE_RESULT_ERROR_DEVICE_LOST`.

Driver, process-access and trust cleanup passed. The topology helper could not
restore its temporary clone because native recovery had already removed the
indirect adapter; its failed result is preserved. This is not a passed live trial.

The failed 41,988,760-byte request succeeds in a fresh worker. Its rendering header
fields and entire body match an earlier qualified request; only request identity
fields differ. All 20,736,000 output components match the CPU reference exactly.
The evidence supports a failure dependent on preceding execution state, rather
than that particular draw being intrinsically unsupported. It does not establish
the underlying driver/runtime cause.

## Performance candidate and failed broader qualification

An opt-in specialization recognizes opaque, unmodulated, nearly integer-translated
linear samples. A conservative coordinate-error calculation rejects cases whose
possible byte error reaches half a quantization step. The selected device performs
pixel extraction, channel ordering, alpha replacement and background merging.
Scaled, point-filtered, colored and other unsupported cases retain the general
sampler. Device execution errors propagate; they do not trigger silent fallback.

On the captured 2880 × 1800 draw, CPU and NPU output match the existing reference
exactly. Three NPU executions take 673.370, 259.248 and 172.159 ms, excluding the
initial general-model setup. The preceding general sampler took roughly 7.5 seconds
per draw. These are individual-operation timings, not desktop frame rates.

The broader CPU oracle passes 286,728,192 component checks across clipping,
channel exchange, checkerboards and random data. The NPU fails with device loss
when the suite reaches a large full-screen graph. Subsequent isolated controls
also vary: a shared OpenVINO Core passes one large-only run but fails the full
suite; separate Core instances fail both tested configurations. Consequently the
whole-screen crop optimization is **not qualified for integration**.

## Shared runtime candidate

Source v17 adds an opt-in shared Core for NPU compilation and serializes its
mutable configuration. Defaults remain unchanged. A lifetime/independent-request
check, the existing region check and eight sampler checks pass. The diagnostic
probe and eight real registered D3D11 NPU operations also pass, with staging and
cleanup verified. A full desktop trial is a separate requirement.

OpenVINO recommends one Core per application because separate objects instantiate
separate plugins. This motivates the experiment; it does not prove a driver fix.
[OpenVINO Core documentation](https://docs.openvino.ai/2023.3/api/c_cpp_api/group__ov__runtime__cpp__api.html).

An upstream Lunar Lake report on the same NPU driver documents similar device
loss. Its October 1 follow-up shows that turbo, keep-alive submissions and disabling
idle pruning do not reliably prevent failure, including under heavy input writes.
That is supporting context from different hardware, not a diagnosis of Delta.
[OpenVINO issue #38403](https://github.com/openvinotoolkit/openvino/issues/38403).

## Reproduction and evidence

`native-routing-v17.patch` applies after the v16 candidate archived in
[the batch investigation](../native-batch-boundary-v1/README.md). `sources/` contains
the isolated crop candidates, numerical proof, replay and preparation scripts.
The raw archive preserves failures and successes, including 184 text artifacts
whose 836,903 bytes were checked against hashes generated on Delta. Desktop
pixels, binary captures, compiled models and signing keys are excluded.

See [report.json](report.json) and [raw-manifest.json](raw-manifest.json). A component
pass does not establish sustained rendering, physical scanout, interaction,
application-wide GPU migration or improved Tahoma performance.
