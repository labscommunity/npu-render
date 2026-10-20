# Command-batch capacity and isolated NPU performance hints

These are completed component experiments on Delta. They do not replace the
passing desktop baseline, implement a Windows queue, or measure Tahoma.

## Capacity: preserve execution lanes

The original compiler-generated Softmax graph provides six 256 KiB input/output
spans on six SHAVE execution engines. Larger rows compile and pass original-graph
checks through 640 KiB, but the compiler schedules them on three engines. Each
engine reuses its local addresses for two sequential tasks. One MiB and two MiB
rows fail compilation; those failures and the passing before/after health checks
are preserved.

Two replacement rectangle kernels pass 120 exact fixtures each, with original
model checks before and afterward. Only bounded executable bytes change; compiler
metadata, allocations, DMA operations and scheduling remain intact. The wider
kernel uses six 516 KiB rows on three engines. A second graph supplies twelve
258 KiB rows on all six engines, two sequential tasks per engine.

Three A/B/B/A rounds compare identical ordered solid-rectangle work at five
resolutions. All 140 complete comparison images match the independent integer
oracle. At 1024×768 with 32 commands:

| Candidate | Existing six-row median | Candidate median | Requests |
| --- | ---: | ---: | --- |
| Six wider rows, three engines | 47.299 ms | 80.090 ms | 3 → 1 |
| Twelve smaller rows, six engines | 45.508 ms | 42.713 ms | 3 → 1 |

The first candidate is rejected for performance. The second improves this
component's median by 6.14%, while small images regress. Its 1920×1080 and
2880×1800 medians improve from 108.533 to 104.908 ms and from 259.182 to
250.766 ms. Each median has six observations; the archive retains every sample,
including early outliers. The result still exceeds a 33.3 ms frame budget before
textures, driver traffic or presentation.

The later twelve-row, 256 KiB-per-row graph also passes its original Softmax
checks. No custom kernel or renderer is tested on that exact graph here.

## Performance hint: isolate the actual default

The first hint test imports exactly one model in each of eight separate child
processes. It compares explicitly false and true `NPU_TURBO` settings in ABBA and
BAAB order. Its historical `default` label means **explicitly false**, not an
unspecified setting. All eighty changing complete images match.

A second experiment tests unspecified settings, explicitly false, and explicitly
true in all six permutations. Each mode appears twice in each position. Every
process renders ten changing 1024×768 images; the first two are warmups.
All 180 images match, including matching input and output hashes across modes.

| Import setting | Processes | Warm observations | Median |
| --- | ---: | ---: | ---: |
| Unspecified | 6 | 48 | 81.831 ms |
| Explicitly false | 6 | 48 | 81.600 ms |
| Explicitly true | 6 | 48 | 40.100 ms |

These observations are clustered within six processes per mode. Untimed CPU
reference preparation creates gaps between NPU jobs, unlike the uninterrupted
capacity comparison; their absolute timings must not be compared directly.
Frequency, power and shared-package effects are unmeasured.

The first post-test health tool fails before inference because querying an
unspecified `NPU_TURBO` property is unsupported. Its failed report is preserved.
Corrected tools record that unavailable readback explicitly and each pass eight
original-model calculations. No global Windows power policy changes.

OpenVINO documents turbo as a hint for NPU frequency and memory throughput within
the platform power limit, with possible effects on other compute resources.
Consequently, a renderer candidate must pass recorded workload and desktop tests,
then a matched iGPU/Tahoma comparison before this becomes a useful policy for our
goal. A faster isolated NPU can still be the wrong package-power tradeoff.
[OpenVINO NPU properties](https://docs.openvino.ai/2026/openvino-workflow/running-inference/inference-devices-and-modes/npu-device.html#supported-features-and-properties).

## Evidence and scope

`artifact-sha256.json` pins 256 artifacts: 237 hash-verified remote files and
nineteen local inspection, preparation and analysis files. No compiled binaries,
device models, pixel packets or caches are committed. Thirty-six and sixty-six
offline contract checks precede the two new kernels' hardware execution.

The accompanying original compiler graph probes, bounded ELF inspections,
CPU pixel fixtures, hardware receipts and exact comparison outputs remain
separate forms of evidence. None establishes general textured batching,
resource-alias safety, Windows submission semantics, sustained desktop
responsiveness, GPU relief or Tahoma improvement.
