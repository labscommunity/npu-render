# Masked texture rendering in the live Windows desktop

Worker v6 passes native live v41. The physical-output presenter submits 49 frames
matching their NPU source records, over 537.684 seconds, with 34 distinct source
pixel hashes. There are 1,011 acknowledged NPU operations, one provider generation
and no faults or retries. The custom paths admit 25 masked-texture draws,
435 shared geometry draws, 18 four-sample draws, five solid draws and one
three-texture draw. Masked and shared geometry models each import once.

Blue, green and red are observed on the owned physical output. Mouse, keyboard,
relative motion, confinement and two pointer reacquisitions pass. Four fresh
physical-output samples are observed near completion. Ordinary topology, windows
and cursor state are restored, with memory integrity enabled.

The first observed green follows the mouse receipt by 8.41143 seconds; red follows
the key receipt by 45.3 ms. These two observations include sampling cadence and
are not a latency distribution, panel photon timing or a matched comparison with
v40. The component speedup has not established responsive desktop interaction.
[Controlled response derivation](controlled-response-analysis.json) preserves
the timestamp calculation.

The presenter issues 98 GPU copies, including 49 validation readbacks. The
physical-output observer adds 146 measurement copies. These are command counts,
not GPU time or power. Physical scanout remains on Intel display hardware.
Unleased default Direct3D device creation still fails with `887a0004` for both
feature lists. Automatic application rendering, browser/video/game coverage,
Tahoma benefit and this renderer's thirty-minute duration gate remain open.

Legacy raw `desktop_offloaded` and `input_forwarding_tested` fields remain false.
Explicit native DWM execution, matching physical-output observations and
`normal_windows_input_tested` qualify this bounded trial. This is not full desktop
offload or a usable switch. Source and reports are preserved byte for byte;
executables, model blobs and pixel images are excluded.
