# Windows desktop composition and Tahoma contention

Delta's existing DWM renderer used the Arc B390 GPU during a controlled window
motion trace. Moving three otherwise static windows reduced Tahoma throughput by
a median **7.33% across five paired rounds** relative to the same windows standing
still. Four pairs were slower and one was faster. This establishes a workload to
target, not an NPU offload result or a prediction of recoverable performance.

The [Windows integration assessment](../../../research/windows-compositor.md)
describes what is still required to replace DWM's rendering.

## Experiment

Hardware and driver state are unchanged from the [activated-driver checks](../README.md):
Core Ultra X7 358H, Arc B390 driver 32.0.101.8724, NPU driver 5540, Windows 11.
The capture records the Balanced power scheme. No new graphics/NPU driver was
installed, no Windows compositor was replaced, and no user application was closed.

`nr_desktop_motion` creates three 900×650 GDI windows. Each window paints once;
the process either keeps them stationary or calls `SetWindowPos` at a nominal
60 ticks/s along a repeatable path. Every trial recorded exactly three paint
calls. DWM renders the changing arrangement of real windows. This is controlled
OS activity, not a recording of the user's normal day and not the earlier NPU
blend demo. The two conditions include the same windows.

`benchmark_desktop.py` ran five counterbalanced pairs, three requests per condition,
after one warmup. The existing Qwen2.5-7B-Instruct INT4 model ran in Tahoma's
OpenVINO GenAI GPU worker with prefix caching disabled. All thirty measured
requests returned 128 tokens and the same output hash. Every trial passed the
active `WinSta0/Default` desktop checks before and after inference. The window
loops completed normally. The benchmark's owned worker and temporary tasks were
stopped/removed afterward.

The exact executed Python harness is retained under `raw/`; the committed version
additionally retains a pending trial when a post-trial quality gate fails. That
evidence-preservation change does not affect this successful trial sequence.

## Measured results

GPU values below are the median across five trial means, each calculated from
3–4 valid independently primed query intervals wholly inside the inference phase.
They refer to DWM's single Arc 3D engine, not total GPU utilization.

| Metric | Stationary windows | Moving windows |
|---|---:|---:|
| DWM Arc 3D engine, observed sample utilization | 0.032% | 3.249% |
| Pooled median Tahoma tokens/s, 15 requests | 24.298 | 23.836 |
| Pooled median time to first content | 151.82 ms | 149.53 ms |
| Window content repaint calls per trial | 3 | 3 |

The five paired moving/stationary throughput ratios were **0.9722, 1.0321,
0.9267, 0.9246 and 0.9157**. Their median is 0.9267. This statistic is deliberately
distinct from dividing the pooled request medians (which gives about a 1.9%
reduction); the short run has noticeable drift/variability. Neither statistic
establishes a general desktop penalty. See [derived results](desktop-summary.json)
and the complete request timings in the raw benchmark.

Window motion changes CPU/window management, GPU scheduling, memory traffic and
possibly clock/power behavior together. Their individual contributions were not
measured. The earlier application NPU comparison regressed, so even this observed
contention cannot be treated as an automatic NPU benefit. A real replacement must
remove this work and beat its own transfer/synchronization/memory costs.

## Counter validation and adapter attribution

DXCore maps `luid_0x00000000_0x0000dd11` to the hardware Arc B390 and
`luid_0x00000000_0x0000fea2` to the software Microsoft Basic Render Driver.
These identifiers are valid for this boot/configuration only. DWM had instances
on both; the measured active 3D engine belonged to the Arc GPU.

Tahoma's busy engine in this capture is labelled `Neural` under the **Arc GPU's
LUID**. The name alone must not be interpreted as NPU execution.
[Microsoft's engine/adapter interpretation](https://devblogs.microsoft.com/directx/gpus-in-the-task-manager/).

The initial collector captured ninety samples but expanded wildcard process
instances only once. It observed DWM activity when a positive-control window
opened but missed the new renderer process. Its raw evidence is preserved as
`control-counters.*`, and it is not used to rank newly started applications.
[PowerShell's implementation](https://github.com/PowerShell/PowerShell/blob/master/src/Microsoft.PowerShell.Commands.Diagnostics/GetCounterCommand.cs)
performs wildcard expansion before its sampling loop.

The corrected collector refreshes instances before every independently primed
sample. The seventy-five-sample positive control discovered `nr_native_live`
after capture began and observed nonzero Arc 3D activity in five samples, averaging
4.94%. Both twenty-second GPU controls completed 1,200 successful submissions,
without skipped slots or deadlines, on the Default desktop. They validate telemetry,
not existing-workload offload. See [positive-control analysis](dynamic-summary.json).

The desktop run collected 140 samples from 19:47:57 to 19:55:06 UTC, much longer
than 140 seconds because query discovery/priming adds gaps. Twenty-eight engine
readings had invalid PDH status 3221228486 and belonged to processes whose identity
was no longer available; all are excluded. Other attributed readings have valid
statuses. The collector retained two corresponding error messages. No missing or
invalid reading is converted into a zero. Counter statuses 0 and 1 are valid per
[Microsoft's PDH documentation](https://learn.microsoft.com/en-us/windows/win32/perfctrs/pdh-error-codes).
See [full engine analysis](engines-summary.json).

## Presentation evidence and limits

Official PresentMon 2.6.0, verified against its GitHub release SHA-256
`b2a706bc6ad475749e3b7e3409263aa1e6906d45bdcf993f6dbc0f660188f1af`, captured
6,390 DWM records over a separate 300-second interval using a unique ETW session.
All records report `Hardware: Legacy Flip`; 6,387 have numeric display latency
and three report `NA`. This is event-based evidence, not physical scanout timing
or a proof that every submitted window movement became a visible frame.

The CSV's `--date_time` timestamps are seven hours behind the recorded local clock
(fourteen behind UTC). PresentMon 2.6.0's
[time conversion source](https://github.com/GameTechDev/PresentMon/blob/v2.6.0/PresentData/PresentMonTraceSession.cpp)
applies local-time conversion when storing its start time and again when formatting
timestamps. This explains the observed offset, but we retain the original bytes
and do **not** align those rows to individual trial phases by guessing a correction.
Use QPC timestamps with an explicit UTC/QPC calibration for a subsequent trace.
[PresentMon's documented GPU timing limitations](https://github.com/GameTechDev/PresentMon#tracking-gpu-work-with-hardware-accelerated-gpu-scheduling-enabled)
also apply; GPU frame timing is not used to decompose the throughput result.

## Reproduce the analysis

`manifest.json` records both the original-byte and gzip hashes for 23 captures.
Decompress `raw/*.gz` into an ignored results directory, then run:

```sh
python research/analyze_desktop_capture.py results/desktop-capture --output results/desktop-summary.json
python tools/analyze_gpu_telemetry.py results/desktop-capture/desktop-counters.jsonl --adapters results/desktop-capture/adapters.json --output results/engines-summary.json
```

The [benchmark instructions](../../../live-benchmark.md#measure-windows-existing-compositor)
describe a new capture. A private repository and Conventional Commits remain the
shipping policy. Whole-desktop NPU execution and improved Tahoma performance
from that execution remain unachieved.
