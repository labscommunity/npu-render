# Packaged runtime validation

On 2026-10-06 UTC (October 5 in America/Chicago), the built `npu-render` 0.1.0 wheel
was installed into the isolated research environment on delta. The machine
reported Core Ultra X7 358H, Arc B390, Intel AI Boost, Windows 11 build 26200, and
OpenVINO 2026.1.0. The original research inventory records the NPU driver as
32.0.100.4724 and GPU driver as 32.0.101.8724; this task did not replace drivers.

One renderer successfully switched **GPU → NPU → CPU → GPU → NPU** for 96×64
composition with two foreground layers and a background, using FP16 NCHW inputs.
Every preparation passed the fractional/transparent/opaque qualification cases.
After every switch, the explicit rendered test frame matched the independent
oracle within the declared tolerance and reported the requested device family.

[Captured report and tested wheel hash](delta-runtime-switch.json).

These calls validate the packaged provider and switching path. They are not a
display benchmark or evidence of improved Tahoma performance. The original
standalone probe and its larger performance study remain separate artifacts.

The local macOS CPU suite also passed 27 checks covering composition, layouts,
precision, output lifetime, concurrent preparation, failed-switch preservation,
explicit device attribution, invalid inputs, configuration and CLI behavior.

Reproduce on a machine with the corresponding devices:

```sh
uv run python tools/validate_switch.py --backends GPU NPU CPU GPU NPU --report results/switch.json
```
