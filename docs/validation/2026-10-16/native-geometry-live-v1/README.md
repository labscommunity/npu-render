# Shared geometry in the live Windows desktop

Worker v5 passes native live v40. The physical-output presenter submits 49 frames
matching their NPU source records, over 549.828 seconds, with 33 distinct source
pixel hashes. There are 1,082 acknowledged NPU operations, one provider generation
and no faults or retries. The custom paths admit 477 shared geometry draws,
20 four-sample draws, five solid draws and one three-texture draw. The shared
geometry model imports once.

Blue, green and red are observed on the owned physical output. Mouse, keyboard,
relative motion, confinement and two pointer reacquisitions pass. The freshness
validator observes four fresh physical-output samples near completion. Topology,
ordinary windows and cursor state are restored, with memory integrity enabled.

The first observed green follows the mouse receipt by 5.79099 seconds; red follows
the key receipt by 235.8 ms. These two events include observation cadence and are
not a latency distribution, panel photon timing or a matched comparison with
earlier trials. Responsiveness remains unqualified. The derivation is retained in
[controlled-response-analysis.json](controlled-response-analysis.json).

The presenter issues 98 GPU copies, including 49 validation readbacks. The
physical-output observer adds 134 measurement copies. These are command counts,
not GPU time or power. Physical display scanout remains on Intel display hardware.
Unleased default Direct3D device creation still fails with `887a0004` for both
feature lists. Automatic application admission, browser/video/game coverage,
Tahoma benefit and this renderer's thirty-minute duration gate remain open.

The raw reports' legacy `desktop_offloaded` and `input_forwarding_tested` fields
remain false. Explicit native DWM execution, physical-output observations and
`normal_windows_input_tested` describe this bounded qualification. This does not
establish full desktop offload or a usable switch. The manifest preserves source
and reports byte for byte; executables, model blobs and pixel images are excluded.
