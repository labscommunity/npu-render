# Power-corrected repeat and interference controls

Delta's new power connection reverses the previous battery decline. During the
unchanged-executable repeat, Windows battery remaining capacity rises from 17,389
to 21,239 and charge rises from 22% to 27%. AC remains connected. This is stronger
evidence of positive battery balance than the charging flag alone; it does not
measure negotiated adapter watts or guarantee an unconstrained processor budget.

The repeat still finds a Tahoma regression: original NPU composition is **4.62%
slower** than the GPU shader and the optimized convolution graph is **4.71% slower**,
using median paired throughput ratios across five rounds. Every NPU/GPU pair is
below one. The previous battery decline is therefore insufficient to explain the
offload regression. Comparing these numbers with the older 6.3% result does not
isolate an improvement caused by the power change; those are separate runs.

## Repeat with the same executable

The executable SHA-256 is
`012c3cef43f013e6c3741c7a83e4851e7be92f7b30960fe5093668d8bedd3a5f`,
identical to the [preceding cost investigation](../slowdown/README.md). The source
is from `4a2f5a638f7a957718fdd4fd417b3062b89fa3d8`. The model, prompt, GPU inference
settings, driver versions, Windows boot and Balanced power scheme are unchanged.
The harness rotates GPU/NPU/NPU_CONV for five rounds, with three 128-token requests
per trial, two settling seconds and at least 120 seconds of extra inference warmup.

| Median of trial statistics | GPU shader | Original NPU | Convolution NPU |
|---|---:|---:|---:|
| Tahoma tokens/s | 23.596 | 22.878 | 22.590 |
| Composition wall time | 0.623 ms | 5.509 ms | 3.574 ms |
| Mean sampled GPU clock | 1,822 MHz | 1,779 MHz | 1,737 MHz |
| Mean GPU energy-derived power | 9.017 W | 8.506 W | 8.241 W |

These descriptive condition medians differ from within-round paired ratios.
The original-NPU/GPU ratios are **0.93825, 0.96826, 0.94424, 0.97498, 0.95383**;
the convolution-NPU/GPU ratios are **0.92294, 0.99413, 0.93809, 0.96013, 0.95292**.
Convolution/original has median **0.99348**, with mixed direction. The cheaper graph
again does not produce a corresponding inference benefit.

All 45 measured requests produce 128 tokens with the same output hash. All 16,703
active frame submissions return `S_OK`, with no skipped slots or processing
deadline misses. Scanout is not measured. GPU power-limit flags persist under the
new power connection. The [full repeat summary](baseline-summary.json) retains
per-trial clocks, energy, phase times, CPU consumption and paired comparisons.

## Controls that separate elapsed time from resource demand

A second build adds explicit diagnostic controls, without changing the default
blend graph or rendering path. Its SHA-256 is
`3d86093647f48c85639045336b7e4f707abb882eabd71d7102300298f4c3f620`.
A six-condition smoke test passes before a sustained run with 123 seconds of extra
warmup, five rotating rounds and three 128-token requests per condition. All 90
measured completions have the same hash as the first run. Battery capacity rises
monotonically from 21,432 to 28,346, and charge rises from 28% to 37%.

| Paired Tahoma comparison | Five ratios, in round order | Median change |
|---|---|---:|
| NPU blend only / idle | 0.96734, 0.96698, 0.97941, 0.96517, 0.96611 | **−3.30%** |
| GPU plus wait / GPU | 0.99817, 0.98259, 0.99831, 1.00063, 1.02091 | **−0.17%** |
| Transfer/presentation control / idle | 0.98214, 0.97451, 0.98202, 0.96094, 0.96593 | **−2.55%** |
| Full GPU renderer / idle | 0.93831, 0.94162, 0.93596, 0.96394, 0.92957 | **−6.17%** |
| Full convolution NPU / GPU | 0.97504, 1.00113, 1.01122, 0.97444, 0.98486 | **−1.51%** |
| Full convolution NPU / NPU blend only | 0.94578, 0.97488, 0.96636, 0.97319, 0.94761 | **−3.36%** |

`NPU_ONLY` repeatedly runs the actual convolution blend at 60 iterations/s on
inputs uploaded once. It makes **zero repeated presentation calls** and skips
animated scene generation and per-frame input upload. It still reduces Tahoma
throughput in all five pairs. The median renderer CPU consumption is 0.0166 logical
core equivalents, close to idle's 0.0154. This is direct evidence of interference
from the NPU execution path even after removing those surrounding stages. It
does not separate NPU arithmetic, device memory traffic, firmware or driver costs.

`GPU_WAIT` keeps the full animated GPU trace and adds a paced host wait after its
blend. The requested wait is 3 ms; measured median waits are 3.43 ms, reflecting
timer wakeup overhead. Median complete frame work becomes 6.63 ms, comparable to
the full NPU path's 6.69 ms, versus 3.16 ms for ordinary GPU rendering. Yet the
paired inference effect is near zero and mixed in direction. Therefore comparable
extra elapsed frame time, by itself, does not reproduce the NPU interference.

`TRANSFER_PRESENT` still generates/uploads the scene but skips the blend and
presents an unchanged output. It also slows Tahoma versus idle in every pair.
It is deliberately not equivalent animated graphics: its static output can change
DWM behavior. Its result and the NPU-only result **must not be added** to estimate
the full pipeline's cost. They establish that both paths can interfere, not an
additive breakdown of the original slowdown.

The six-condition experiment's full NPU/GPU comparison varies more than the
unchanged-executable repeat: three pairs regress and two improve slightly.
Different workload histories, power allocation and ordinary variation can affect
the result. The experiments establish neither a fixed percentage penalty nor a
repeatable benefit. We did not isolate which factor explains the difference
between the two runs.

## What the telemetry can and cannot explain

In the control run, medians of trial mean GPU clocks are 1,935 MHz for idle,
1,892 MHz for NPU-only, 1,766 MHz for full GPU graphics and 1,792 MHz for full NPU
graphics. GPU power-limit flags occur in 100/151 idle samples, 108/156 NPU-only
samples, 118/159 GPU samples and 118/162 full NPU samples. Thus stronger external
power does not remove the reported GPU power constraint.

These observations are consistent with competition for platform resources.
They do not prove that power sharing alone causes the slowdown. For example,
the first NPU-only/idle pair has similar mean sampled GPU clocks (1,892/1,888 MHz)
but 3.27% lower inference throughput. These are sampled current-clock readings,
not effective execution-clock measurements. The interface still supplies no supported
memory-bandwidth, temperature, effective-clock or NPU/package-energy readings.
Power allocation, memory/cache traffic and scheduling cannot be assigned individual
percentages from this capture.

The first-principles issue is the amount of work actually retired. The prototype
removes one inexpensive GPU blend dispatch, retains copies and DWM, and introduces
a full-image NPU graph with conversion and intermediate tensors. Moving arithmetic
to a different engine is useful only if its remaining shared-resource costs are
less than the costs removed. Meeting 60 Hz or shortening NPU latency is insufficient
to establish that inequality for Tahoma.

The next implementation target is less NPU and transfer work per displayed frame:
damage-aware processing, retained surfaces and fewer intermediate representations.
Compare identical output and apply common work-reduction optimizations to both
backends. Any favorable result must then survive a sustained matched comparison
and integration that actually retires existing compositor work. Windows DWM and
arbitrary application graphics have not been redirected by this experiment.

## Correctness, cadence and recovery

Every trial passes desktop, loaded-driver, full-shape pixel and initial
presentation checks. All repeated presentations return `S_OK`. Normal GPU,
GPU_WAIT and full NPU conditions run approximately 60 submissions/s with zero
skipped slots and zero processing deadline misses. NPU-only runs approximately
60 iterations/s, with 5,332 compositions and no repeated presentations.

TRANSFER_PRESENT records one deadline miss in round three and one skipped slot
in round four, both during inference; these remain in the raw results. Its rates
range from 59.942 to 59.997 submissions/s. Other controls have no skips or misses.
The analyzer distinguishes iterations, compositions and presentations; an omitted
presentation has a null HRESULT rather than a success value.

After throughput testing, GPU → NPU → CPU → GPU selection and invalid-AUTO recovery
pass on the same executable. The invalid choice retains NPU rendering, and all
presentation calls return `S_OK`. The installed prototype hash is unchanged.
All owned benchmark workers, telemetry processes and scheduled tasks are stopped;
the two test ports have no listeners. No driver, power plan, clock or fan setting
was changed by this investigation.

## Reproduction and provenance

The [native diagnostic instructions](../../../../native/interop.md#diagnose-npu-composition-cost)
describe the new conditions. The [control summary](controls-summary.json) retains
every paired result, phase statistic and matched telemetry interval. Original
bytes, executed Python harnesses, changed C++ sources, build hashes, server logs,+battery samples and cleanup records are compressed in `raw/`; [manifest.json](manifest.json)
records original and compressed SHA-256 values. Unchanged native dependencies are
from the baseline revision above. [Provenance](provenance.json) identifies both
executables and the source files used for the analysis.

After decompressing raw files into an ignored directory:

```sh
python research/analyze_native_cost.py results/power-retest/baseline.json results/power-retest/baseline-igcl.jsonl --output results/power-retest/baseline-summary.json
python research/analyze_native_cost.py results/power-retest/controls-sustained.json results/power-retest/controls-igcl.jsonl --output results/power-retest/controls-summary.json
```

The summaries reproduce byte-for-byte. The earlier cost investigation's summaries
also remain byte-identical with the updated analyzer. Local validation passes all
75 Python tests, Ruff checks and formatting checks. MSVC builds the new executable
on Delta, and the six-condition smoke run plus normal control/recovery test pass.
Physical scanout, total system energy, and general desktop offload remain unmeasured.
