# Activated driver, native presentation and Tahoma

An explicitly authorized Windows Restart completed on Delta at
2026-10-07 17:34:30.500 UTC. Both the installed Intel NPU package and the loaded
OpenVINO driver now identify version 5540 (`32.0.100.5540` / `1005540`). Earlier
captures with the package installed but driver 4724 still loaded remain unchanged.

The exact installed [ee4d711 bundle](../2026-10-06/bundle-ee4d711/README.md) remains
the application under test. Its executable SHA-256 is
`2a1165c7fb08b989c4a97c0d00bd9fe8ca6df7d7709044b1227d2c643c17b5fd`.
Renderer OpenVINO is 2026.1.0; the native contention harness uses the desktop
preflight introduced in source revision `96e4953`.

## Pixel correctness and live switching

The installed bundle passed full-shape GPU/NPU startup qualification at 1280×720
and 1920×1080, requiring loaded driver 1005540. Maximum difference from the
independent integer oracle was one byte value. These validation-only calls do not
exercise presentation.

The 720p control validator then ran as the normal desktop user in connected session
1, `WinSta0\Default`, without allowing occlusion. It selected GPU → NPU → CPU → GPU
and rejected an invalid AUTO selection while retaining NPU rendering. Every initial
and repeated presentation returned `S_OK`: 365 GPU, 484 NPU and 296 CPU frames.
Median complete processing was 4.27, 8.87 and 5.45 ms respectively. The control run
recorded one skipped replay slot and four processing deadline misses overall.

This establishes successful native submissions on an active desktop. It does not
measure physical scanout, replace Windows DWM or move other applications' rendering.
The saved global selection and installed shortcut were preserved.

## Native Tahoma comparison

The existing Cascadia/Tahoma executable ran Qwen2.5-7B-Instruct INT4 through OpenVINO
GenAI 2026.3.1 on the GPU. Its startup log records compilation on GPU; its module
snapshot includes the GPU plugin and no NPU plugin. The CPU plugin was also loaded,
so module enumeration alone does not establish every operation's execution device.
The executable hash, runtime versions and launch settings are retained. Its exact
source revision was not exposed by the binary.

The machine used its Balanced power plan. Continuous batching used a 1 GB KV cache,
one sequence and disabled prefix caching. Five rounds rotated GPU/NPU/CPU/IDLE
conditions, with three streaming requests per condition. All sixty requests
returned 128 tokens and the same output content hash. The native graphics trace
used one foreground surface at 1280×720 and a target 60 Hz. GPU composition used a
D3D12 compute shader, rather than the earlier OpenVINO GPU graph.

Every trial passed the loaded-driver gate, desktop preflight before/after inference,
and all presentation checks. The three active conditions submitted 16,763 frames
with **zero skipped slots and zero processing deadline misses**. IDLE presented
once per trial and did not repeatedly render. Statistics include the two settling
seconds at the start of each trial; durations and therefore frame counts differ.

| Graphics condition | Median end-to-end tokens/s, 15 requests | First content, median ms | Complete frame work, median / p99 ms | Submitted frames |
|---|---:|---:|---:|---:|
| D3D12 GPU shader | 23.99 | 148.53 | 2.96 / 4.53 | 5,387 |
| NPU shared buffers | 22.78 | 153.19 | 7.49 / 9.64 | 5,763 |
| CPU integer blend | 22.75 | 149.61 | 7.97 / 10.55 | 5,613 |
| IDLE | 25.19 | 141.74 | No repeated rendering | 0 |

The per-round NPU/GPU median throughput ratios were 0.9414, 0.9721, 0.9496, 0.8953
and 0.9104. The primary paired statistic is **−5.9%**, ranging from **−10.5% to
−2.8%**. The ratio of pooled medians is a different statistic. This run establishes
a regression for this trace and stack, not a useful Tahoma acceleration. The earlier
small gains against the OpenVINO GPU baseline do not override the native result.

NPU composition itself took a median 5.07 ms, versus 0.58 ms for the shader. The
graphs, memory traffic, host waits and shared power budget are candidates for
explaining the loss; this run contains no engine, bandwidth or power telemetry to
assign causality. Chunk arrival times are client observations, not hardware token
timestamps. A single five-round run is not evidence about every desktop workload.

The benchmark server and temporary tasks were stopped and removed after testing.

## Wider interfaces and compiler investigation

After activation, the runtime-independent DXCore NPU workload query still returned
zero adapters. The generic-ML query listed only Arc B390 and the Microsoft software
renderer. No NPU Direct3D device or shader execution was established. These results
describe the installed interface; they are not a fundamental limit on NPU arithmetic.

`research/compiler_probe.py` compares explicit PLUGIN and DRIVER compilation in
separate bounded processes. Each case first requires the loaded driver, checks a
CPU control against an independent oracle, then verifies the NPU execution device
and resolved compiler. It stops the sweep after a timeout or device-loss signature.
Small operation success is not a full-frame performance or interoperability result.
OpenVINO documents the separate compiler choices in its
[NPU device guide](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html).

Both OpenVINO 2026.1.0 and the isolated 2026.3.1 environment produced the following
small-fixture results, using explicitly selected and reported compilers:

| Case, 64×64 source | Plugin compiler | Driver compiler |
|---|---|---|
| ReLU control | Exact | Exact |
| FP16 bilinear GridSample, 32×32 output | Pass, max normalized error 0.000244 | Same |
| FP32 bilinear GridSample | Fail, max error 0.014965 against tolerance 2/255 | Same |
| Convolution packing, unpacking, both | Pass, max byte error 1 | Same |
| Four-pixel grouped layout | Pass, max byte error 1 | **Fail, max byte error 254** |

At 1280×720 under the pinned plugin compiler, convolution packing and combined
packing/unpacking failed with maximum byte error 255. Unpacking alone passed but
took roughly 18–19 ms after the first call. The existing grouped layout passed;
its host-tensor calls took roughly 7.4–7.6 ms. Those timings include Python
inference/copy work and are not the native shared-buffer timing. These failures
rule out a blind compiler or layout substitution. No production compiler setting
or installed renderer was changed.

The first compiler capture was interrupted by PowerShell treating native stderr
as a terminating error. A subsequent probe incorrectly split a string-valued
`EXECUTION_DEVICES` into characters and rejected each case before inference. Both
captures are retained as harness failures, not hardware failures. The corrected
probe accepts the observed string/list forms and verifies each requested device.
PowerShell's captured process-exit fields are null in the small-suite status files;
the per-case reports, including failed pixel comparisons, are the evidence.

## Exact-coordinate sampling experiment

Direct FP16 normalized bilinear coordinates cannot retain arbitrary higher-precision
subpixel positions. An alternative
uses nearest-neighbor GridSample to fetch four exact texels, then applies explicit
FP16 weights and source-over arithmetic. Padding each source dimension to a power
of two at most 2048 makes `(2*x+1)/extent - 1` exactly representable in FP16 for all
integer texel centers. The probe rejects larger extents. It preserves arbitrary
indexed texels within that bound; it does not assume bilinear coordinate precision.

The new `grid_nearest` research variant passed independent integer-byte checks with
changing images, indices and weights on CPU and NPU, for a 67×49 source/16×16 output
and a 640×360 source/128×128 output. Maximum byte error was one. A matched,
unprofiled comparison on activated driver 5540 found warm inference/copy calls of
**17.9–20.3 ms for planar Gather versus 26.0–33.0 ms for GridSample**. A separate
profile attributed 25.799 ms to SHAVE GridSample. Profiling entries can overlap;
their durations must not be summed into an assumed frame cost. Padding and grid
construction are host work excluded from these call timings.

This is a correct alternative addressing representation, but not a faster sampler.
It remains an opt-in research probe and was not connected to the native runtime.
No device-loss signature occurred during the completed compiler/sampler sweeps.

```powershell
uv run python research/compiler_probe.py --expected-npu-driver 1005540 `
  --cases relu grid_f16 grid_f32 pack unpack both channels_4
uv run python research/sampler_layout_probe.py --expected-npu-driver 1005540 `
  --compiler PLUGIN --variants planar_f16 grid_nearest --width 640 --height 360 `
  --side 128 --frames 5
```

## Native regression qualification

The native NPU composition, indexed sampling and 97-case resize suites passed with
maximum byte error one; the Python supervisor captured exit zero for each. The
resize suite emits a compiler diagnostic for its intentional rejected preparation
and confirms rollback/alias/switch behavior. A preliminary PowerShell wrapper had
mistakenly treated its null exit-code property as a failure despite a successful
native report; `native-confirm` and `native-check-status.json` retain the reliable
supervised rerun.

The 640×360 → 1280×720 general sampling benchmark passed eight changing frames with
57 dispatches per frame and API times of 578–652 ms. The specialized resize/compose
path passed with one dispatch and API times of 11.7–12.1 ms. Both require host source
preparation and are off-screen calls, not desktop delivery. The generic path remains
unsuitable for full-frame 60 Hz rendering.

## Remaining scope

The private prototype provides a working switch within participating renderers.
System-wide Windows composition, client drawing/text, arbitrary game shaders,
general GPU compute, codecs and native Linux NPU presentation are still open.
The next performance gate is to reduce measured operation/transfer costs and repeat
the native comparison; the all-functions objective has not been achieved.

`manifest.json` records hashes of the unchanged decompressed captures. Large raw
reports are gzip-compressed; adjacent summaries are derived, not replacements for
the raw observations. Job launch files are excluded from the evidence bundle.
