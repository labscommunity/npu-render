# Reusing Windows source surfaces

This follows the [first bounded handoff](../../2026-10-07/desktop-delegate/README.md).
All tests run automatically on Delta. Tahoma inference stays on the Arc GPU;
the controller Mac only orchestrates the runs. The scope remains three cooperating
opaque GDI windows, with the outer Windows desktop and other applications retaining
their existing paths.

## Changes under test

The original path creates and uploads three 1280×720 foreground planes each frame:
11,059,200 bytes of host pixel upload, plus CPU clearing/placement and GPU copies.
The new path retains the three immutable source images and separate foreground
planes. It clears only old pixels outside each window's new bounds, then copies
the source into its new position. The selected GPU/NPU blend still resolves the
three layers over the background; placement never combines different windows.

Both devices receive the same optimization. Repeated foreground host uploads
become zero, while GPU placement payload falls to roughly 1.3 MB/frame. This does
not eliminate the three full-frame blend calls, final GPU presentation or the
outer Windows compositor. Reported copy bytes are logical payload, not measured
physical DRAM traffic. Retaining buffers also increases allocation footprint.

The first implementation (`--placement retained`) issues row-by-row buffer copies.
This saves bytes but adds many commands. A second implementation
(`--placement retained-rect`) copies each rectangle with `CopyTextureRegion` and
uses 256-byte-aligned source row pitches. Microsoft's
[copy-location contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_texture_copy_location)
supports describing a texture layout in a buffer. The
[copy operation](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion)
permits texture data in buffer or texture resources. Neither path performs hidden
CPU composition or silently changes the requested execution device.

The retained initialization byte count covers source images and the transparent
clear buffer only. It excludes the shared renderer's initialization and all
pixel-qualification work. Live copy counts exclude blend reads/writes and the
final buffer-to-swapchain copy. Source pixels are immutable in this experiment;
application updates and arbitrary window integration remain future work.

## Validation protocol

The real GPU and NPU independently pass full-image opaque-window oracle cases
covering overlap, clipping and offscreen movement. Those transitions also check
that vacated foreground pixels return to zero. Each interactive run compares
Windows/GPU/NPU screen pixels before timing and checks its final displayed frame.
The DWM → NPU → invalid request → GPU → DWM sequence verifies source retirement,
selection retention and restoration. No screen images are retained.

Each performance comparison uses five rotating rounds, three identical 128-token
requests per condition, at least 120 seconds of additional inference warmup, and
30 window updates/s. It includes ordinary DWM, staged GPU/NPU, and both devices
using the new placement method. Device/runtime, power, engine, frame and inference
records accompany the run. Experiments run sequentially; no compilation,
additional graphics test or bundle upload runs on Delta during measured trials.

## Row-copy result

The first five-round run completed 75 requests, all with 128 output tokens and
the same output hash. All 14,417 scheduled window updates completed without a
skipped slot or processing deadline miss. Every independent and displayed pixel
check had zero error. The battery stayed at 100%, with AC online, no charging or
discharging, and remaining capacity unchanged at 76,231 mWh throughout 111 records.

| Placement and compositor | Tahoma tokens/s | Frame processing, ms | Placement wait, ms |
|---|---:|---:|---:|
| Windows DWM | 23.961 | 0.729 | — |
| Staged NPU | 21.250 | 14.359 | 2.420 |
| Staged GPU | 22.922 | 5.956 | 2.448 |
| Retained rows, NPU | 20.336 | 16.815 | 4.767 |
| Retained rows, GPU | 21.725 | 8.762 | 4.581 |

These are medians of trial medians. Frame processing covers the submitting
process, not complete DWM execution or physical scanout. The report's `upload_ms`
field measures waiting for placement submission/fence completion; retained modes
have no live host pixel uploads despite the historical field name.

Within-round throughput ratios show **3.95% lower Tahoma throughput** with retained
NPU rows versus staged NPU, and **5.13% lower** with retained GPU rows versus staged
GPU. All five comparisons regressed for each device. Retained NPU was **15.08%
below DWM**; staged NPU was **10.39% below DWM** in this run. Do not compare those
percentages directly with earlier experiments: only the within-run comparisons
control for their surrounding conditions.

Reducing logical copy payload did not reduce placement time: hundreds of small
row-copy commands added enough overhead to outweigh the smaller transfer volume.
Host timestamps cannot apportion that overhead exactly among command processing,
GPU execution and synchronization. This result motivated the separate rectangle
experiment; it does not justify selecting row retention as the default.

The preserved preliminary `full-clear-prototype` captures check an earlier
retained implementation that cleared whole planes. They are not the implementation
used in this performance table. The failed temporary-directory link and attempted
switch with a missing executable are also preserved. The successful build used
an isolated build directory outside Windows Temp; neither failure was an NPU
execution failure.

Process-attributed counters support the command-overhead explanation. Median
trial-mean delegate 3D utilization rose from **1.47% to 12.61%** for staged versus
retained-row NPU, and **2.56% to 13.28%** for the corresponding GPU paths. DWM's
sampled 3D activity was about 0.273% for the original windows and zero during
delegation. Zero sampled utilization is not proof of no work between samples.
The verified Arc GPU's `neural` engine is Tahoma GPU activity, not the Intel NPU.

The telemetry capture reports 29 queries with invalid counters somewhere in the
system; the analyzer rejects invalid samples individually. No selected
process/adapter samples inside these inference intervals were rejected. Only
15–19 complete query intervals per condition fit entirely inside inference, so
these counters are supporting evidence, not a high-resolution execution trace.
IGCL reports GPU power limiting in every condition but no temperature or current
limit samples in the included intervals. Retained-row throughput is lower even
though its median trial-mean GPU clock is higher than the staged counterpart.
Clock rate alone therefore does not explain this regression, and the capture
does not measure shared DRAM bandwidth or NPU/package power directly.

## Rectangle-copy result

The second run completed another 75 identical 128-token requests, with the same
output hash as the first run. All 13,979 scheduled updates completed at 30/s with
zero skipped slots or processing deadline misses. Every offscreen and screen
pixel check had zero error, preparation identified the NPU graph's device as NPU
and loaded driver as 1005540, and all original windows were restored. The battery remained at 100%,
AC online and capacity 76,231 mWh throughout 109 records, without discharge.

| Placement and compositor | Tahoma tokens/s | Frame processing, ms | Placement wait, ms | Delegate GPU 3D, % |
|---|---:|---:|---:|---:|
| Windows DWM | 23.388 | 0.739 | — | 0.000 |
| Staged NPU | 21.806 | 15.077 | 2.716 | 1.434 |
| Staged GPU | 22.827 | 5.870 | 2.456 | 2.571 |
| Retained rectangles, NPU | 22.242 | 11.679 | 0.633 | 0.627 |
| Retained rectangles, GPU | 23.172 | 3.603 | 0.645 | 1.733 |

Timing and throughput columns are medians of trial medians. Engine percentages
are medians of trial means for one identified Arc 3D engine and the exact delegate
PID. DWM itself averaged about 0.279% on that engine for the original windows and
sampled zero during delegation. Engine percentages must not be summed across
different engine types. The separate process's 0.739 ms Windows-mode update cost
does not include DWM's asynchronous work.

| Paired Tahoma comparison | Median throughput change | Rounds improved |
|---|---:|---:|
| Rectangle NPU / staged NPU | **+2.14%** | 4/5 |
| Rectangle GPU / staged GPU | +1.41% | 4/5 |
| Rectangle NPU / Windows DWM | **−3.09%** | 0/5 |
| Rectangle GPU / Windows DWM | −0.16% | 2/5 |
| Rectangle NPU / rectangle GPU | −3.07% | 0/5 |

The optimization reduces NPU frame processing from 15.077 to 11.679 ms, about
22.5% in these descriptive medians. Live foreground host uploads fall from
11,059,200 to zero bytes per update. NPU rectangle placement copies a median
1,299,048 bytes/update; the one-time source/clear upload is 4,976,640 bytes.
The corresponding NPU process CPU consumption falls from 0.103 to 0.057 core
equivalents. The new copy method therefore addresses a real implementation cost,
but **does not establish an overall Tahoma benefit versus Windows**. Its NPU
throughput remained lower in every paired round. Keep `staged` as the reproducible
default and expose both retained modes as explicit experiments.

The NPU blend stage still takes about 9.522 ms versus 1.607 ms for the GPU version
with the same placement method. These elapsed host measurements include waits;
they are not individual engine execution durations. The two paths have similar
placement/presentation timings, so more copy batching alone is unlikely to remove
the remaining difference. Shared-memory and package-resource interference remain
plausible contributors, consistent with the earlier isolation experiments; this
run does not quantify their individual shares.

The counter collector completed 179 samples and reports 29 queries containing
invalid system counters. One selected DWM-mode engine sample was rejected by the
analyzer; all other selected samples were valid. Only 14–15 complete query
intervals per condition fall entirely inside inference. IGCL again reports GPU
power limiting in every condition and no included temperature/current-limit
samples. This is evidence against a battery-discharge explanation for these
particular results, not evidence that package power limits disappeared.

## Build, package and evidence

The row implementation is commit `af8f9d5`; rectangle batching is `e69e78a`.
Both use Conventional Commits and passed their required Windows/Linux/native/
Weston CI checks. The rectangle revision passes 105 Python tests and Ruff checks.
The downloaded private CI bundle was checked against all 32 manifest entries,
installed into an isolated directory on Delta, and run automatically. The packaged
executable passes GPU/NPU pixel qualification at 640×360 and 1920×1080, plus a
390-update, zero-skip visible DWM → NPU → invalid → GPU → DWM sequence at 1280×720.
All screen comparisons are exact and all three sources are restored. Boundary
sizes are correctness checks, not display/performance claims at those sizes.

The previous launcher and application builds remain unchanged. All owned test
workers, graphics processes, telemetry probes and scheduled tasks were removed;
test ports were free. No reboot, driver, security setting or power-plan changes
were made. The same Windows boot remained active throughout.

The Tahoma worker is the installed `cascadia-ov` GPU worker using OpenVINO GenAI
2026.3.1 and Qwen2.5-7B-Instruct INT4. The renderer uses OpenVINO 2026.1.0 on Core
Ultra X7 358H / Arc B390 / NPU 5. The fixed prompt, request records, execution
device declaration, worker arguments, binary hashes and exact adapter identities
are retained. No Tahoma source changes are involved.

- [Row-copy analysis](rows-summary.json)
- [Rectangle-copy analysis](rectangles-summary.json)
- [Source, executable and package provenance](provenance.json)
- [Raw-file and derived-file checksums](manifest.json)

Raw captures retain original bytes and encodings in deterministic gzip files.
The route probe was captured between the runs on the same boot and its Arc LUID
matches both IGCL captures. The prototype source snapshots exactly match their
recorded commits. Preliminary/failed attempts remain separate from the successful
performance captures. Package checks began only after measurement cleanup.

## What the experiment can resolve

Retaining source images removes recurring host pixel uploads and batching changes
how that same placement work is submitted. It does not make the NPU process less
image area: the compositor still invokes three full 1280×720 blend graphs per
update, with full-size intermediate outputs, even though each source occupies
only 426×240 pixels. There is also a final graphics-buffer presentation and an
outer DWM composition step. The original opaque windows are an inexpensive case
for Windows to handle directly.

If efficient placement still fails to improve Tahoma, the next implementation
must reduce blend area or graph/intermediate passes and compare that optimization
on both devices. It must also retire an equivalent amount of existing compositor
work. Installing a driver wrapper or relabeling the adapter cannot remove these
measured costs. This bounded experiment does not establish support for arbitrary
applications, game rendering, codecs, HDR, fractional window opacity, updated
application surfaces or direct NPU scanout.
