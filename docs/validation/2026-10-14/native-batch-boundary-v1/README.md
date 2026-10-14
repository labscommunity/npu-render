# Full-resolution resource draws and a reproducible NPU batch failure

The new candidate supports the captured 30-vertex, two-texture desktop draw at
2880×1800. A larger-batch experiment also isolates a reproducible NPU failure and
qualifies a bounded configuration. **Full desktop offload remains unfinished.**
These are operation, transport and registered-adapter results, not a usable desktop
or a Tahoma performance measurement.

## Resource-table capacity

ABI 23 admits large resource-table targets within the existing 64 MiB request and
bounded reply budgets. Legacy shader, sampler, geometry and work checks remain.
The captured draw is not byte-identical across CPU and NPU: 48 of 20,736,000 output
components differ by one 8-bit step. Both executions pass the existing independent
scalar oracle, with exact coverage and unchanged pixels outside the scissor. Alpha
is identical. The original exact-comparison failure is preserved, followed by the
explicit comparison report. The acceptance tolerance was not increased.

The local worker receives a 24,992,136-byte request and returns 20,736,128 bytes,
including its receipt. Its pixels match the independently qualified NPU replay
exactly. Renderer v16 passes this delivery check, ten diagnostic operations, and
eight operations through ordinary registered Direct3D. Package 0.0.1312.0 stages,
executes and removes successfully; memory integrity and owned cleanup pass.

Microsoft specifies instruction-dependent floating-point and format-conversion
precision, so CPU equality alone is not a general Direct3D conformance test. Our
one-byte oracle is a project acceptance check, not certification of every shader
instruction against those specifications.
[Direct3D floating-point rules](https://learn.microsoft.com/en-us/windows/win32/direct3d11/floating-point-rules),
[data conversion rules](https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-data-conversion).

## Batch-size experiment

For the earlier six-vertex 2880×1800 draw, all three tested configurations preserve
every output byte. Rows must divide the target height, so requested and actual
batch sizes differ. Times below measure isolated draw execution; compilation is
reported separately and these runs are not desktop or Tahoma A/B measurements.

| Requested capacity | Actual pixels/batch | Tiles | Median draw | Model setup |
| --- | ---: | ---: | ---: | ---: |
| Existing large-target default | 14,400 | 360 | 9,701.381 ms | 420.941 ms |
| 65,536 | 57,600 | 90 | 7,484.463 ms | 4,371.713 ms |
| 262,144 | 259,200 | 20 | 7,170.231 ms | 6,357.009 ms |

The broader 1024×768 sampler regression fails with `ZE_RESULT_ERROR_DEVICE_LOST`
at the first desktop model when capacity is 65,536. Disabling the private shared
queue setting does not prevent it. A fresh-process isolated matrix reproduces
failure for all four sampling modes at capacities 65,536 and 262,144, while all
four pass at the original size and 16,384. Thus preceding model switching is not
necessary to reproduce this particular failure.

A 65,535 ceiling selects 49,152 pixels per batch for 1024×768 and 57,600 for
2880×1800. All four isolated modes then pass, followed by the full four CPU and
four NPU sampler suites plus the region-scheduler check. This does not prove that
every shape below that ceiling is supported or explain the earlier native v13
worker loss. The failed larger settings remain isolated experiments, not defaults.
The first v15 build's stale boundary assertion and its v15b correction are also
preserved.

Profiling shows the affine graph executes many `Shave` operations and DMA tasks.
Host packing takes about 31 ms versus 7,598 ms spent inside inference calls in the
profiled replay. Per-node durations overlap or use driver-specific accounting and
must not be summed as frame time. This supports investigating the graph itself;
reducing call count alone produced only a modest improvement.

## External stability evidence

Intel's download page still lists driver 32.0.100.5540. A separate Lunar Lake report
on that version reproduces device loss under multi-model inference. Later updates
report failures even with turbo, disabled idle pruning and continuous submissions.
Those results weaken a simple idle-power explanation; they do not establish
Delta's cause. No turbo or persistent power setting was changed here.
[Intel driver download](https://www.intel.com/content/www/us/en/download/794734/intel-npu-driver-windows.html),
[upstream report and follow-ups](https://github.com/openvinotoolkit/openvino/issues/38403).

## Reproduction and limits

[report.json](report.json) records assertions over 205 remotely hash-verified text
files (895,259 raw bytes). [raw-manifest.json](raw-manifest.json) identifies originals
and deterministic gzip artifacts. Patches proceed from v13 through v14, v15,
v15b and v16. Source preparation scripts and isolated probes accompany them.
Raw desktop pixels, binaries and model caches are excluded. Default runtime code
is unchanged; native continuous delivery, interaction, scanout verification,
application coverage and Tahoma benefit remain separate gates.
