# Native NPU cost investigation

The current offload replaces a small GPU blend with a more expensive NPU graph.
It retains GPU copies, swapchain presentation and Windows DWM. Removing one GPU
dispatch therefore does not remove the graphics pipeline's resource costs.

The alpha extraction/broadcast/inversion sequence is a confirmed inefficiency.
A sparse 1×1 convolution implements the same channel operation more efficiently
on this NPU. In the first concurrent Tahoma run, median native composition fell
from **5.19 ms to 3.45 ms**, approximately **33% less time**. The GPU shader path
took **0.63 ms**, including dispatch and its host fence wait. The optimized NPU
graph remains an opt-in experiment (`--npu-alpha convolution`); useful Tahoma
acceleration has not been established.

## What the measurements establish

The [earlier native run](../README.md) spent 5.07 ms in NPU composition versus
0.58 ms in GPU composition. Scene generation, input upload and presentation were
similar, accounting for only a small part of the 4.53 ms difference in total frame
work. Compilation and pixel qualification ran before measurement. This narrows
the renderer slowdown to the graph/inference path rather than startup, extra
output readback or frame pacing.

The host-tensor diagnostic records a 1.21 ms DMA Gather, a 1.73 ms DPU Subtract,
and a 0.52 ms layout operation in the baseline profile. The alternate graph
replaces that sequence with a 0.95 ms DPU convolution. Conversion, multiplication,
addition and rounding still remain. Profiling entries can overlap and are **not**
summed into a wall-time estimate. Their `cpu_ms` field is not host CPU utilization.
Native shared-buffer timing independently verifies the improvement.

This is a case where expressing the computation in the NPU's efficient operations
matters more than minimizing arithmetic on paper. A GPU shader evaluates the
per-pixel blend in one dispatch; the original graph introduces multiple full-image
operations and intermediate representations. At 720p, each BGRA8 surface is
3.69 MB and its FP16 representation is 7.37 MB. These sizes describe logical
arrays, **not measured external-memory traffic**: compiler fusion, local memory,
caches and DMA behavior determine the actual traffic.

The first run also exposes a sustained-load transition. Early trials sampled the
GPU at 2,500 MHz with no reported power-limit flag. Later trials have lower clocks
and frequent `gpuPowerLimited` flags, including GPU-only and IDLE-renderer trials.
Windows reported AC power connected and charging, with the Balanced plan active,
but reported battery charge fell during the capture (see below). Power limiting
is observed; the capture does not measure the NPU's
share of package power or prove the NPU caused the transition.

Power limiting alone cannot explain the entire backend difference: in round zero,
both GPU and original NPU conditions sampled 2,500 MHz, but NPU throughput was
5.34% lower. Additional shared-memory traffic, scheduling and cross-device waits
remain candidates. The available IGCL interface reports no supported memory
bandwidth, temperature, effective-clock or NPU/package-energy readings on Delta.
We cannot assign percentages of the remaining regression to these mechanisms.

All first-run active conditions submitted approximately 60 frames/s, with no
skipped slots or processing deadline misses. Renderer CPU consumption was about
0.12 logical-core equivalents across GPU and both NPU conditions. This is no
evidence of host CPU saturation or of an inference gain hidden by fewer frames.
It does not exclude subtler CPU scheduling or memory effects.

## Graph correctness and rejected shortcuts

`research/blend_cost_probe.py` compares five graphs against an independent integer
source-over oracle, with changing premultiplied foreground **and background**.
Eight frames include fully transparent, fully opaque and randomized alpha.
OpenVINO reports actual NPU execution, the PLUGIN compiler and loaded driver
`1005540`. All variants stay within one byte, but their errors differ:

| Graph | Median host call/copy time after two initial frames | Mean signed byte error |
|---|---:|---:|
| Original gather | 10.120 ms | −0.000006 |
| Convolution, original rounding | 8.514 ms | −0.000006 |
| Add 0.5 instead of Round | 8.938 ms | +0.041756 |
| Truncate instead of Round | 9.078 ms | −0.326860 |
| Convolution plus add 0.5 | 7.398 ms | +0.041756 |

The host diagnostic includes output copy and conversion to int16 for checking;
its absolute times must not be compared with native shared-buffer times. Cheaper
rounding variants introduce systematic brightness bias despite passing a loose
one-byte gate. The implemented convolution path keeps the original rounding.

Native GPU/NPU shared-buffer qualification passes at 1280×720 and 1920×1080, with
maximum error one byte. These validation-only calls ran in session zero and do
not establish presentation. The expanded native CPU graph test passes 48 cases
covering both alpha modes, divisible/nondivisible widths and pixel groups 1/4/16/32.
CPU checks establish graph semantics, not NPU execution.

The alternate graph also passes the live GPU → NPU → CPU → GPU selection-file
test on the Default desktop. An invalid AUTO selection is rejected while NPU
rendering continues, and all presentation calls return `S_OK`. Its executable
matches the hash used for the throughput runs.

## Concurrent inference protocol

Hardware remains Core Ultra X7 358H / Arc B390 / NPU 5010, Windows 11, NPU driver
5540 and GPU driver 32.0.101.8724. The renderer uses OpenVINO 2026.1.0. The same
existing Tahoma binary and Qwen2.5-7B-Instruct INT4 model run through OpenVINO GenAI
2026.3.1 on the GPU, with one sequence, 1 GB KV cache and prefix caching disabled.
No driver, power-plan or clock setting was changed.

The first experiment rotates five GPU/NPU/NPU_CONV/IDLE rounds, three identical
128-token requests per trial, with two settling seconds. All sixty completions
have the same hash. The twenty trials pass desktop, loaded-driver, pixel and
presentation-status gates; 16,533 active frames return `S_OK`, with zero skips
and zero processing deadline misses. This measures submissions, not physical
scanout. NPU profiling is disabled during throughput measurements.

| First experiment, paired median throughput ratio | Result |
|---|---:|
| Original NPU / GPU | 0.94659 (−5.34%) |
| Optimized NPU / GPU | 0.96282 (−3.72%) |
| Optimized NPU / original NPU | 1.00086 (+0.09%) |
| GPU / IDLE renderer | 0.96361 (−3.64%) |

The direct optimized/original comparison is mixed: three pairs improve and two
regress. The difference between two median ratios is not itself an optimization
gain. The [complete first-run analysis](first-summary.json) retains every paired
ratio, request statistic, frame-phase statistic and telemetry interval.

## Sustained repeat and power constraints

A second five-round GPU/NPU/NPU_CONV run follows an additional 24 warmup requests
lasting 123 seconds. All 45 measured requests return 128 tokens and the same
output hash; all fifteen trials pass their gates. The active renderers submit
16,753 frames at approximately 60 Hz, with zero skips, zero processing deadline
misses and every presentation returning `S_OK`.

| Sustained repeat | GPU shader | Original NPU | Optimized NPU |
|---|---:|---:|---:|
| Median of trial median composition times | 0.706 ms | 5.198 ms | 3.475 ms |
| Median of trial median complete frame times | 3.618 ms | 7.955 ms | 6.100 ms |
| Median of trial median Tahoma tokens/s | 24.216 | 22.294 | 22.674 |
| Median of trial mean GPU clock readings | 1,806 MHz | 1,737 MHz | 1,738 MHz |
| Median of trial mean GPU energy-derived power | 8.980 W | 8.122 W | 8.284 W |
| Renderer live CPU core equivalents, median | 0.122 | 0.117 | 0.115 |

The paired original-NPU/GPU throughput ratios are **0.9060, 0.8984, 0.9388,
0.9334, 0.9382**, median **−6.66%**. Optimized-NPU/GPU ratios are **0.9370,
0.9688, 0.9363, 0.9384, 0.9354**, median **−6.30%**. Both NPU paths are slower
than GPU in every pair. The direct optimized/original ratios are **1.0342,
1.0784, 0.9973, 1.0054, 0.9970**, median **+0.54%**, with mixed direction.
See [the complete sustained analysis](steady-summary.json).

The reduction in blend time is repeatable, while a useful Tahoma benefit is not.
The optimized path still performs conversion, blend, rounding, shared-buffer
handoffs and presentation. Making an already-on-time frame finish sooner is not
equivalent to releasing the resources that limit token generation.

GPU power-limit flags are true in 114/156 GPU-condition samples, 131/169 original
NPU samples and 125/166 optimized NPU samples. The lower GPU clocks under the NPU
conditions support investigating power allocation, but do not isolate it from
memory or scheduling. The energy values cover the GPU counter's domain only;
lower GPU watts do not establish lower system energy or greater efficiency.

There is also a power-delivery confound: an earlier snapshot reports 21% battery,
the sustained run begins at 18% and ends at 16%, and the post-run snapshot reports
15%. All thirty before/after checks during the sustained trials report AC
connected and the charging flag set. These readings contradict the assumption
that AC presence alone guarantees adequate sustained power. They do not identify
the negotiated USB-C wattage or distinguish an undersized supply, cable/port
issue, firmware policy or battery reporting error.

The short follow-up runs six Tahoma GPU requests with no NPU renderer. Raw battery
remaining-capacity readings fall from 11,967 to 11,694 while the firmware continues
reporting `Charging=true` and `Discharging=false`; the first recovery sample is
11,678. These contradictory status/rate fields must not be interpreted as proof
of charging or used to calculate input adapter watts. This reproduces the capacity
decline without NPU graphics, narrowing the power issue to something broader than
the new blend graph. All owned workers and tasks are stopped afterward.

Delta identifies as ASUS Zenbook S14 UX5406AA. ASUS specifies a
[68 W USB-C adapter for that model](https://www.asus.com/ca-en/laptops/for-home/zenbook/asus-zenbook-s-14-ux5406/techspec/).
A repeat with confirmed adequate power and stable/recovering charge is needed
before generalizing these throughput percentages. None of these experiments
replaces DWM or establishes the cost of a future full desktop implementation.

## Reproduction and provenance

See [native usage](../../../../native/interop.md#diagnose-npu-composition-cost).
The collector uses Intel's read-only Graphics Control Library, pinned to commit
`b6c462933502e13d1537dd5024949a51be30e63d`; [source hashes](igcl-source.json) identify
the exact external header and wrapper. No setters are called and no IGCL/driver
binary is redistributed. V2 returns status `0x40000001`; the legacy telemetry call
succeeds. Each metric retains support, type, units and original value.

The [API contract](https://github.com/intel/drivers.gpu.control-library/blob/b6c462933502e13d1537dd5024949a51be30e63d/include/igcl_api.h)
defines GPU energy and activity as monotonic counters. Analysis checks units and
valid API status, rejects counter resets, and computes rates only across adjacent
samples wholly within the inference phase. It matches phases with captured host
Unix time, using the device timestamp only for deltas. Unsupported readings stay
null. Adapter-wide counters cannot attribute activity or energy to one process.

The [OpenVINO NPU documentation](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html)
also describes a shared platform TDP constraint and possible impact on other
compute resources when NPU turbo is enabled. This experiment does **not** enable
turbo; that documentation is context, not evidence of the measured cause.

`manifest.json` records original and compressed SHA-256 values. Decompress the
raw captures to an ignored directory and reproduce the first analysis with:

```sh
python research/analyze_native_cost.py results/slowdown/native-contention.json results/slowdown/igcl-contention.jsonl --output results/first-summary.json
python research/analyze_native_cost.py results/slowdown/native-steady.json results/slowdown/igcl-steady.jsonl --output results/steady-summary.json
```

The exact executed harnesses are retained. The committed harness additionally
rejects nonfinite or excessive warmup durations; that validation change does not
affect these runs. Source and binary hashes identify the native build. The installed
prototype and saved global selection remain unchanged.
