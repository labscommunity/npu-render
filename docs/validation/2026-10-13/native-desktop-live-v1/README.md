# Native desktop execution reaches the NPU; no desktop frame yet

Delta was signed in and unlocked for these October 13 trials. The actual Windows
compositor executed NPU operations through the native diagnostic adapter, but
exited before the indirect driver could read back a desktop frame. **Full desktop
offload remains incomplete.** Both trials removed their diagnostic adapters,
packages and temporary trust, restored the owned process permissions exactly,
and left memory integrity enabled. The physical output was never disabled.

| Trial | Actual DWM NPU operations / delivery ACKs | Large target operations | Desktop frames | Recovery |
| --- | ---: | ---: | ---: | --- |
| Admission v1, renderer 1303 | 13 / 13 | 0 | 0 | Passed |
| Admission v2, renderer 1304 | 21 / 21 | 7 | 0 | Passed |

These counts include repeated device initialization attempts. They are not frame
counts or a performance comparison. Native WUDFHost entered the indirect driver
and selected the requested render adapter in both trials. Neither trial produced
the required native indirect frame; neither tested a complete physical desktop
or Tahoma throughput.

## Diagnosed and repaired transport limit

The first captured rejected draw targets a 2880×1800 BGRA surface: 5,184,000
pixels, exceeding the transport's previous 4,194,304-pixel limit. Offline CPU and
NPU replay both succeed with identical output. The draw has 24 vertices and zero
covered samples, so this is a correctness and transport check, not evidence of
fast full-screen rasterization.

The isolated native-routing-v5 candidate adds ABI 16 for larger untextured draws.
It preserves the existing 32 MiB packet ceiling, limits the reply to that ceiling,
binds the wrapped payload ABI into reply identity, and retains previous limits
for clears and textures. Nine rejection cases and the existing indexed-exchange
contract pass. The captured request then passes through the C++ local client and
real NPU worker: all **20,736,000 pixel bytes** match the CPU reference, with an
NPU receipt and delivery ACK. Ten component operations and eight operations
through the normal registered D3D11 runtime also pass.

The second actual DWM trial confirms seven of those large requests execute and
return successfully. Its next textured draw is rejected with `0x80070057`. The
bounded diagnostic capture is incomplete, so its precise cause is still under
investigation. The large-target transport fix does not complete desktop support.

## Physical presentation is a separate partial result

The presentation probe initially rejected an immutable source texture descriptor.
Adding a shader-resource bind flag to the immutable texture, while keeping the
staging texture unbound, fixes that call. Trials v5 and v6 verify exact backbuffer
pixels and submit 1,802 and 1,800 presents respectively over 15 seconds each.
They display a previously verified static NPU frame from the VM experiment;
they do not display a live native NPU desktop.

PresentMon reports lost ETW events and produces no CSV in either trial. Therefore
independent flip, hardware-overlay engagement and reduced GPU composition remain
unverified. Successful presents and composition capability flags do not establish
those outcomes.

## Evidence and remaining work

[report.json](report.json) and [raw-manifest.json](raw-manifest.json) retain 186
remotely hash-verified text artifacts. Patches retain the tested changes; the
failed C++ build, obsolete-version qualification rejection, failed texture
creation and unsuccessful presentation traces are preserved. Captured desktop
pixels, executables, model caches and credentials are excluded. These changes
remain isolated experiments and are not promoted into the default runtime.

The next requirement is a stable native compositor frame. Physical frame
delivery, desktop placement, input, recovery of the complete topology, useful
interactive latency and matched Tahoma measurements follow. Earlier tests found
Tahoma regressions; this work has not established that moving desktop rendering
to the NPU will improve inference throughput.
