# Direct3D shared textures on Delta, October 8

**Same-driver NPU sharing passes. Native GPU texture consumption fails. Full
Windows desktop offload remains unimplemented.** This work does not alter the
installed desktop renderer or establish improved Tahoma throughput.

## Final source and hardware checks

Final build: `ddi-shared-v19`, Windows 11 Pro build 26200, Core Ultra X7 358H,
Arc B390 driver 32.0.101.8724, Intel AI Boost driver 32.0.100.5540,
OpenVINO 2026.1.0 (NPU reports 1005540). Visual Studio 2022/MSVC 14.44 and WDK
26100.6584 headers compile the explicitly selected DLL. No graphics adapter,
kernel driver, boot setting or display preference was installed/changed.

[The qualification supervisor](qualified-19/supervisor.json) preserves source,
executable, DLL and OpenVINO runtime hashes. Its nine native source hashes and
supervisor hash were checked against the committed files. Each raw report is
hashed in the supervisor. The result is scoped to the same experimental DLL in
two processes, not to DWM or native vendor graphics-driver compatibility.

| Check | Result |
|---|---|
| 64×48 BGRA, 16 changing frames | Pass, ≤1 byte per channel |
| 320×240 RGBA, 16 changing frames | Pass, ≤1 byte per channel |
| 1280×720 BGRA, 16 changing frames | Pass, ≤1 byte per channel |
| 2880×1800 BGRA, 16 changing frames | Pass, ≤1 byte per channel |
| Unaligned shared texture width | Rejected before inference |
| Creator releases original resources, then reopens child-retained legacy handles | Pass at every positive shape |
| Worker and parent release all mappings/aliases | Zero live surfaces/aliases and zero cleanup failures |
| Original DLL CPU/GPU/NPU draw and three rejection checks each | All 12 pass |
| Native GPU opens a legacy shared handle | HRESULT 0; capability observation only |
| Native GPU reads/writes the imported texture | Fails the pixel oracle |
| Native GPU-created texture write/read control | Pass, exact pixels |

The [original-DLL regression report](regression-19/supervisor.json) retains all
12 runs. Six targeted Windows CTests, including CPU control/rejection on the new
DLL, also passed. Local Python verification passed 108 tests and Ruff check/format.
These controls do not substitute for the failing graphics-consumer check.

The same-driver fixture calls ordinary `CreateTexture2D(MISC_SHARED)`,
`GetSharedHandle`, `OpenSharedResource`, draw/update/copy and staging-map APIs.
The shared DLL forwards allocation/open/destroy requests to real KMT functions
on the exact NPU LUID. Standard section allocations preserve the runtime blob and
real legacy resource identities. A bounded footer records our own format/extent.
The child creates local NT aliases over the mapped pages and imports those aliases
as NPU remote input tensors. No synthetic handle is substituted for an OS resource.

Each positive worker executes exactly 16 NPU draws, with zero explicit host input
copies and zero fixture uploads/copies in the worker. It retains one staged output
copy per draw: 331,776,000 bytes over 16 full-display frames. The parent uploads
fixtures and copies for its independent oracle. Host events plus synchronous
inference/readback serialize ownership. Kernel/driver internal traffic is not
measured; neither total zero-copy nor native fence interoperability is claimed.

## Failed native graphics consumer

[The stronger supervisor](gpu-readback-19/supervisor.json) intentionally remains
**failed**. Both small formats open successfully on the exact Arc LUID, but GPU
readback is zero and differs by up to 255 bytes. An update/readback through that
imported GPU texture also fails. Creating a normal texture on the same GPU and
performing the same write/read operation produces exact pixels. These observations
localize a failure in the imported graphics-resource path; they do not establish
its exact cause or a vendor-driver defect.

A [GPU allocation-owner differential](controls/gpu-pixels-15.jsonl) changed only
the KMT allocation owner to GPU while retaining NPU computation. It failed the
same pixel test. This earlier experimental binary is not the final NPU-owner build.
The [handle-only control](controls/gpu-open-19.jsonl) is retained to demonstrate
why successful `OpenSharedResource` alone is insufficient.

[Earlier large native-GPU controls](controls/large-gpu-failures-18/supervisor.json)
terminated the probe with `0xc0000005` at 720p and full display resolution. The
final probe rejects these GPU diagnostic sizes before device creation; its GPU
supervisor now repeats only the two small fixtures. Same-driver NPU round trips
at large sizes are separately qualified. The child is attached to a job with
kill-on-close so a parent crash cannot leave that owned worker running.

Windows' optional graphics debug layer was absent (`0x887a002d` when explicitly
requested). Installing Graphics Tools through the existing SSH session returned
Access Denied; no debug capability or security setting was changed. Correctness
results here come from independent pixel checks, not debug-layer assertions.

## Retained development controls

- `shared-create-03`: KMT creation returned a real resource/allocation/share
  identity; the software runtime callback returned an allocation but omitted its
  `hKMResource` output. Requiring both outputs incorrectly rejected a valid case.
  Destruction now uses the original runtime resource handle through its callback.
- `shared-create-08`: checking immediately after COM release found live resources.
  The final harness uses `ClearState`/`Flush` to force deferred Direct3D destruction,
  then requires all lifetime counters and cleanup results to be clear.
- `gpu-pixels-17`: imported-texture failure plus the successful native-GPU texture
  control, before final diagnostic size guards were added.

## Remaining system integration

The driver still recognizes one shader pair; it cannot execute DWM's full shader
and state stream. It is not a registered/selectable graphics adapter, and its
native GPU texture-consumption gate fails. Presentation, native ownership/fences,
DWM selection, recovery under real desktop workloads and simultaneous Tahoma
measurements therefore remain open. A successful same-driver resource round trip
must not enable or advertise a full-desktop switch.

References: Microsoft's [allocation callback contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-_d3dddicb_allocate),
[resource sharing](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-opensharedresource),
[deferred object destruction](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush),
and [allocation usage/coherency responsibilities](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking).
