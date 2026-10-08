# Windows composition handoff and NPU scheduling follow-up

## Follow-up to the power investigation

Changing NPU execution priority or compiler tile count did not establish a useful
Tahoma gain. The experiment used the existing Tahoma worker on **Delta's Arc GPU**
throughout. Only the separate graphics process changed its rendering backend or
NPU settings. No inference ran on the controller Mac or on the NPU.

The pinned OpenVINO headers describe `EFFICIENT` as lower execution priority;
the name does not promise lower energy consumption. `NPU_TILES=1` constrains the
compiler allocation, while the runtime reports a maximum of three tiles. We did
not measure physical tile utilization or change Windows' power plan.

Five rotating rounds compared six conditions, with three identical 128-token
requests per trial and 121 seconds of extra inference warmup. All 90 measured
requests produced the same output hash. All 27,495 active frame submissions
returned success, with zero skipped slots and zero processing deadline misses.
Battery capacity stayed at 76,231, charge at 100%, AC connected, and discharging
false. Physical scanout was not measured.

| Graphics condition | Median trial Tahoma tokens/s | Median blend time | Paired throughput vs GPU |
|---|---:|---:|---:|
| GPU shader | 23.770 | 0.667 ms | reference |
| NPU convolution, defaults | 23.324 | 3.579 ms | −1.88% |
| NPU lower priority | 22.560 | 4.389 ms | −5.83% |
| NPU one tile | 23.461 | 6.475 ms | −1.50% |
| NPU one tile, lower priority | 23.378 | 8.775 ms | −1.65% |
| Idle graphics control | 24.975 | — | GPU/idle: −4.77% |

Paired ratios compare within-round request medians; they need not equal ratios of
the descriptive condition medians. Against the default NPU graph, lower priority
regressed 0.74% (all five pairs), one tile improved 0.72% with mixed direction, and
one tile plus lower priority improved 0.34% with mixed direction. These small,
inconsistent differences do not justify changing the default settings. Fewer
tiles increased graph latency substantially without producing a reliable Tahoma
benefit. This is consistent with the preceding interference controls, but does
not separately quantify memory, firmware, scheduling or shared power costs.

The GPU still reports power limitation in 115/158 GPU-rendering samples,
121/163 default-NPU samples and 96/151 idle samples. Median trial mean GPU clocks
are 1,784, 1,794 and 1,933 MHz respectively. Stronger external power and a full
battery do not eliminate the reported constraint. The lower-priority NPU path
has 1,749 MHz and 128/164 power-limited samples; these observations do not isolate
the cause of its slower Tahoma result. IGCL still lacks supported memory-bandwidth,
effective-clock and NPU/package-energy measurements on this configuration.

## Windows graphics-driver boundary

A new probe looks up each OpenVINO device's exact Windows adapter LUID. The NPU
**is found by DXCore**, even though workload-filtered enumeration omitted it in
the older probe. Its installed driver reports no D3D12 graphics, core-compute or
generic-ML attributes. Creating a D3D12 device at all three tested feature levels
returns `DXGI_ERROR_UNSUPPORTED` (`0x887a0004`). DXGI cannot enumerate that NPU
as a graphics adapter (`0x887a0002`), so no D3D11 creation is attempted on it.

The same probes succeed on the exact Arc GPU adapter. They never substitute a
default adapter. This establishes a concrete missing route in the current driver;
it does not prove that a custom NPU-backed graphics driver is impossible. No
kernel driver was installed, and no driver or security settings were changed.

## What the Windows handoff changes

`nr_desktop_delegate` owns three 426×240 opaque GDI windows inside a 1280×720
workspace. Their static content and time-based positions are identical across
modes. In DWM mode they are real visible windows, and Windows composes them.
In NPU/GPU modes the originals are hidden after a replacement frame is ready;
the selected device resolves the same overlaps into one shared output surface.
Returning to DWM shows the originals again. Client painting stays on the CPU,
and placement, upload, final GPU presentation and the outer DWM remain.

This retires the three original Windows surfaces during delegation. It is a
cooperating-window integration boundary, not a capture/postprocess of an already
composed desktop. It does **not** replace `dwm.exe`, move other applications or
transfer all desktop work to the NPU. The switch has this explicit bounded scope.

Both source-over and opaque-select pass eight independent full-image offscreen
oracle cases and DWM/NPU/GPU on-screen checks with **zero differing color bytes**.
The visible sequence DWM → NPU → invalid request → GPU → DWM passes for both
graphs. The invalid request retains NPU mode, original-window counts are 3/0/0/3,
and the final restored DWM screen matches the oracle. The source-over switch
run skips 3 of 780 slots; opaque-select skips 35. These runs establish switching
and correctness, not a sustained 60-fps guarantee.

The screen qualification initially failed because of opening transitions and
rounded canvas corners. Only the owned test windows now disable those effects;
the pixel tolerance was not weakened. The screen-saver-blocked attempts were
also rejected. No screen images are retained. The existing native application's
shared-renderer extraction separately passes GPU → NPU → rejected AUTO → CPU →
GPU control/recovery, with all presentation calls returning `S_OK`.

The opaque-select graph uses only exact alpha 0/255 selection. The NPU compiler
rejects `Equal` and `Select` on byte tensors, so the working graph converts to
FP16, selects without arithmetic, and converts back to bytes. Both rejected
variants and compiler logs are retained. CPU tests cover aligned and non-grouped
widths; real NPU tests cover the actual 1280×720 shape. Despite fewer high-level
operations, Select is slower on this stack. Operation count alone is not a
hardware cost model.

## Sustained Tahoma result with the actual handoff

At 60 updates/s, the Select smoke trial delivered 200 iterations in 3.705 seconds,
skipping 22 slots and missing 193 processing deadlines. The harness rejected it.
Every condition was then repeated at **30 updates/s**, followed by 122 seconds of
extra inference warmup and five rotating rounds of three 128-token requests.
All 60 measured outputs have the same hash. The 11,546 iterations at 30 updates/s
have zero skipped slots and zero processing deadline misses. All initial/final
screen checks are exact. No physical scanout timing is available.

| Condition | Median trial Tahoma tokens/s | Median frame work | Three composition calls | Sampled DWM 3D activity |
|---|---:|---:|---:|---:|
| Windows moves original windows | 23.829 | 0.736 ms (host window movement) | Windows owns composition | 0.281% |
| GPU delegated composition | 22.637 | 6.010 ms | 1.608 ms | 0% median |
| NPU delegated composition | 21.788 | 14.569 ms | 9.940 ms | 0% |

The NPU/DWM paired ratios are **0.92384, 0.94551, 0.86726, 0.94232, 0.89942**:
**7.62% lower Tahoma throughput**, with every pair slower. GPU delegation/DWM
has median **0.93268** (−6.73%), but mixed direction. NPU/GPU delegation has median
**0.94678** (−5.32%), with every pair slower. These percentages are separate paired
comparisons, not additive contributions to a causal breakdown.

The source handoff is observable: original HWNDs are absent in delegated modes,
and sampled DWM 3D activity falls from about 0.28% to zero. However, the delegated
process itself now uses the GPU: median trial mean 3D-engine samples are **2.545%**
for GPU composition and **1.446%** for NPU composition, versus zero for the original
DWM-mode process. NPU mode still issues GPU transfers and presentation commands.
Retiring a small amount of DWM graphics work does not establish net GPU relief.

These are separately identified process samples on the same verified Arc adapter.
Only three or four independently primed queries fit wholly inside each trial;
zero samples do not prove continuous absence of work. The capture contains 162
queries, with metadata retaining transient invalid-counter warnings. None of the
selected target-process/adapter engine samples are rejected. We do not infer an
independent-flip presentation mode or physical display cadence from these counters.

Median trial mean GPU clocks are 1,875 MHz for DWM, 1,835 MHz for delegated GPU,
and 1,784 MHz for delegated NPU. Battery stays at 100% and capacity 76,231 throughout.
This again separates the observed regression from battery drain, while leaving
memory traffic, platform power sharing and driver/scheduling costs incompletely
separated. Tahoma inference runs on Delta's **Arc GPU in every condition**.

The opaque Select alternative is worse too. One final-round Select trial overlaps
a 0.572-second upload of the private bundle; it is retained but excluded from the
Select-specific conclusion. The four unaffected Select/DWM pairs have median
**0.82607** (−17.39%), and Select/default-NPU has median **0.90419** (−9.58%).
All are slower. The full unfiltered summary and
[explicit exclusion/sensitivity record](select-transfer-sensitivity.json) are both
preserved. No DWM, GPU or default-NPU inference trial overlaps that transfer, so
the primary five-round comparisons above are unaffected. Select is not promoted.

The engineering result is a functioning bounded Windows handoff with a negative
performance result. It is useful for establishing and optimizing the integration
boundary, but it should not be enabled as a Tahoma acceleration setting.

## Logical traffic and the next optimization boundary

Each 1280×720 BGRA plane contains 3,686,400 bytes. This first handoff implementation
rebuilds and uploads three full-canvas foreground planes every frame, then makes
three sequential device composition calls. The NPU calls logically consume two
planes and produce one each: 33.18 MB/frame before internal graph tensors. The
three input transfers add 11.06 MB and final output copying adds 3.69 MB, totaling
47.92 MB/frame of logical payload, or 1.44 GB/s at 30 updates/s. This is API-level
traffic accounting, **not measured physical DRAM bandwidth**; caches, shared
memory and compiler intermediates affect actual traffic.

The same three-call/transfer pipeline is used in delegated GPU mode. Comparing
GPU delegation with DWM therefore measures the cost of this integration path;
comparing NPU with delegated GPU adds the change in composition execution. DWM
can reuse the original static surfaces instead of uploading full planes every
frame. Equal visible output does not imply equal implementation efficiency.

The next implementation should retain source buffers and communicate position,
z-order and damage separately, then compose only changed/visible regions. Common
optimizations must be applied to both GPU and NPU baselines. A fused graph can
reduce inter-call synchronization, but its effect on memory traffic and Tahoma
must be measured. Wider desktop coverage remains contingent on an application
surface adapter or the separate graphics-driver work; this experiment does not
make arbitrary Windows surfaces available before composition.

## Reproduction and packaged validation

The experiment began October 7 UTC; delegation measurements ran October 8 UTC.
Hardware and boot are unchanged: Core Ultra X7 358H, Arc B390, NPU 5010, Windows
11 Pro 26200, GPU driver 32.0.101.8724, loaded NPU runtime version 1005540.
The renderer uses pinned OpenVINO 2026.1.0. The Tahoma worker, model and arguments
are retained in the server capture: `cascadia-ov` v0.2.4, Qwen2.5-7B-Instruct-int4-ov,
explicit GPU and the installed OpenVINO GenAI runtime. The controller only
orchestrates; both graphics and inference execute on Delta.

Code revision `bda4b9835f2a85eb48d2428c9fc67e0161be3905` passes 102 Python tests,
six local native CPU tests, Windows/Linux native CI and existing Weston integration
checks. [Private CI](https://github.com/t8/npu-render/actions/runs/37706145103)
builds a portable bundle with the new executable, direct adapter probe and launcher.
All 32 manifest files were checksum-verified locally and after extraction on Delta.
The packaged binary then passes the visible DWM → NPU → invalid request → GPU →
DWM sequence, with 390 iterations at 30 updates/s, zero skipped slots, exact screen
pixels, source retirement and restoration. It is installed in an isolated build
folder; the preceding stable application binary is retained.

On Delta, run `%LOCALAPPDATA%\npu-render\run-delegate.cmd`. It opens the three
cooperating test windows and starts in DWM mode. Press **1 DWM / 2 NPU / 3 GPU**;
Escape closes the experiment. This launcher does not switch arbitrary applications.

The [provenance](provenance.json) identifies executable/source hashes and the
[manifest](manifest.json) records original and compressed hashes for raw captures.
Raw reports include rejected preparation/cadence attempts, exact frozen settings
sources, supervisor commands, worker logs, telemetry, screen checks and cleanup.
The original prototype and system power/driver/security settings remain unchanged.

To reproduce the derived summaries after extracting the raw gzip files:

```sh
python research/analyze_native_cost.py settings-sustained.json settings-igcl.jsonl --output settings-summary.json
python research/analyze_delegate.py CAPTURE_DIRECTORY --benchmark delegate-sustained-30.json --output delegate-summary.json
```

See [native usage](../../../../native/interop.md#cooperating-windows-surfaces),
[benchmark procedure](../../../live-benchmark.md#cooperating-window-handoff-on-windows)
and the [Windows driver assessment](../../../research/windows-compositor.md).
