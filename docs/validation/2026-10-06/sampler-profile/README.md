# Indexed sampling: host staging and kernel profiling

The general sampler now uses owned host tensors and the same byte/FP16 lookup
tables as the resize path. Input pixels are snapshotted before execution, and all
output batches are staged before destination writes. The graph, sampling contract,
explicit device selection and 128×128 NPU batch limit are unchanged.

The native sampling test passed on Delta's CPU, GPU and NPU. It now checks
nonlocal, in-place source-over across two batches against an independent double
precision oracle, including exact preservation of untouched bytes and padding.
The final reports explicitly include `alias_checked`. This establishes pixel
behavior, not presentation or improved Tahoma inference.

## Complete API cost

Eight changing frames sample a 640×360 source into a 1280×720 destination. CPU
coordinate preparation is separate; packing, graph calls, conversion and commit
are inside `api_ms`.

| Device | Dispatches/frame | Complete API range |
|---|---:|---:|
| NPU | 57 | 612–690 ms |
| GPU | 15 | 29–36 ms |
| CPU | 15 | 31–38 ms |

All outputs passed with at most one byte of error. The earlier NPU capture was
720–886 ms; these separate runs are not an interleaved statistical comparison.
The improvement is insufficient for full-frame transforms. The specialized
resize path remains the useful route for eligible axis-aligned scaling.

`NPU_RENDER_TRACE_SAMPLING=1` reports host preparation, inference and staging
times. In the final NPU frame, inference consumed 604 ms of the 681 ms API call.
Changing host conversion alone cannot close this gap. CPU loop timing also
varies between device runs; these captures do not isolate power/cache causes.

## Per-operation result

The standalone probe uses changing random addresses, texels and interpolation
weights. At 128×128 output, OpenVINO `PERF_COUNT` reported:

| Operation | Execution unit | Reported real time |
|---|---|---:|
| Indexed Gather | SHAVE | 19.231 ms |
| Two multiplies | SHAVE | 0.353 and 0.111 ms |
| Neighbor reduction | DPU | 0.198 ms |
| Subtract / final add | DPU | 0.032 / 0.042 ms |

The second complete infer/copy call was 21.001 ms. The first profiled call was
203.210 ms and is retained. Per-operation timings are diagnostic and must not be
summed as a guaranteed wall-clock decomposition. This fixture uses random
addresses; the native compositor benchmark has different access locality.

The dominant measured operation is texture fetch, not matrix arithmetic. See
[the custom-kernel investigation](../../../research/shave-kernels.md) for the
source-level path this motivates.

## Rejected alternatives and recovery

- Smaller planar source patches (65×65 and 129×129) did not provide a useful
  speedup at the tested output sizes. This does not implement or validate a
  compositor atlas/cropping optimization.
- Neighbor placement along image width and a rank-five layout passed at 128²,
  but remained around 19–25 ms per infer/copy call. Both failed compilation at
  256² with the retained segmented-tiling diagnostic.
- Interleaved FP16 texels caused `ZE_RESULT_ERROR_DEVICE_LOST`. The following
  uint8 experiment returned a driver-applicability error. The sequence was
  stopped, and the known-good shared-buffer blend and native sampler passed
  afterward without a reboot or driver change. These layouts are research-only;
  do not retry them on this stack as part of routine validation.
- GatherND passed a 67×49 source / 16² output fixture within one byte. Larger
  640×360 source cases exceeded 45 seconds at 128² and 30 seconds at 64²; the
  latter emitted no completed-compilation stage. Both were stopped. Neither
  timeout establishes an inference cost or a fundamental hardware limitation.
  The known-good sampler passed after cleanup.
- The initial `ENABLE_PROFILING` spelling was rejected. The successful capture
  uses `ov.properties.enable_profiling()` (`PERF_COUNT`). Both are preserved.

The probe now isolates each variant in a process with a default 45-second limit,
terminates its process tree on timeout, and stops the sequence after timeout or
device loss. It emits compilation/inference stages, checks actual devices and
rejects non-finite output. Forced timeout cleanup passed on Windows with zero
owned workers remaining; a local macOS forced timeout also returned failure.
The default runs only the previously qualified planar layout.

## Reproduction and evidence

```sh
uv run python research/sampler_layout_probe.py --device NPU --profile
build/native/nr_sampling_bench NPU 640 360 1280 720 256 8
```

The raw PowerShell captures are gzip-compressed unchanged. `manifest.json`
records hashes of the decompressed bytes; adjacent JSON normalizes encoding and
extracts reports. Stage logs may contain PowerShell wrapping and diagnostic
envelopes. The empty 256² capture from the aborted first sequence is retained
as an empty file, not a successful measurement. `context.json` records source
hashes and the progression of probe guards. No experimental graph replaces the
installed Windows prototype, and no result here establishes desktop migration.
