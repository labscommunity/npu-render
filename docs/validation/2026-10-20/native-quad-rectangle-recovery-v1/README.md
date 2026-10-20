# Ordered rectangle recovery

Worker22 passes two bounded recovery cases: an injected provider exception and
termination of the owned provider child. Each replays twenty recorded requests
with all twenty output hashes and delivery acknowledgments preserved.

The failure occurs at dispatch 8, original ticket 587, after ticket 467 has
already initialized and used the rectangle provider. Each case records two
provider generations, one failure and one retry on NPU, with no fallback. All
nineteen rectangles across the five selected quad draws execute with ordered
blending and whole-batch preflight.

The tested worker SHA-256 is
`8145fa6d2c85d77d5f79faf45e4fed01f1c851acf0084ba6c40dd2930a48c30c`.
[The report](raw/quad-rectangle-provider-recovery-v1/report.json) and 28-file
artifact manifest retain both cases and their sources.

These are process/provider recovery tests, not induced hardware device loss.
They do not change the [timing conclusion](../native-quad-rectangle-v1/README.md):
targeted draws improve, while aggregate replay time remains unchanged. No desktop
or Tahoma gain is established.
