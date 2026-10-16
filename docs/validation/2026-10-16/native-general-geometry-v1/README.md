# Shared geometry across two desktop shader groups

The geometry kernel now admits up to 2,046 projected vertices per span while
retaining the 3,072-sample output capacity and unchanged coverage/interpolation
arithmetic. The triangle table and task region remain inside the 262,144-byte
input span. All 96 valid and 12 malformed NPU fixtures pass, including larger
vertex tables; original carrier checks pass before and after. The model SHA-256 is
`ea4432abd7b548396b86665c1b318353e0d2f3ec847bcd3e6f56c10bd1336dfa`.

An adapter replaces the existing triangle coverage/interpolation stages while
retaining the original NPU pixel shader and output merger. A shared model and
serialized inference request avoid importing a new geometry model for each
pipeline. Two simultaneously alive geometry adapters produce exactly one import
in each comparison process. A failed shared provider remains failed.

All 51 retained draws from two shader groups pass four comparisons each, with
1,486,061,568 exact byte components. The original-image baseline hashes also match
the qualified worker. The other three cases per draw change textures and
backgrounds. Baseline-first and candidate-first order alternate.

| Shader group | Comparisons | Summed baseline/candidate time | Total reduction | Provider calls, baseline/candidate |
| --- | ---: | ---: | ---: | ---: |
| Two-texture multiply, `d8e0b497…` | 164 | 93.026 / 70.993 s | 23.69% | 19,984 / 15,220 |
| Compositor, `c71638aa…` | 40 | 65.699 / 55.999 s | 14.76% | 4,368 / 3,344 |

Six individual two-texture comparisons and one compositor comparison are slower.
The totals describe these component fixtures, not desktop frame rates or Tahoma
benefit. The derivation is retained in
[comparison-analysis.json](comparison-analysis.json).

The first host build retains the smaller geometry limit and is not run across the
larger captures. The second uses the larger qualified model and passes all 204
comparisons. Its executable SHA-256 is
`073e4f1f0308653b2b8530ba0aeb5e75c968df963947cf642af42d6971f5ffa3`.
The manifest preserves source, both builds, independent fixtures and complete
comparison evidence. Executables, model blobs and pixels are excluded. Worker
replay and live integration remain separate gates.
