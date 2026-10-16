# Combined kernels in the live Windows desktop

Native live v38 passes with the combined worker, including 46 exact matching frame
submissions across 548.34 seconds and 30 distinct source hashes. Windows desktop
composition produces 671 acknowledged NPU operations with one provider generation,
zero faults and zero retries. The broader four-sample path is admitted 11 times;
four of 33 solid-rectangle attempts are admitted. Remaining attempts use the
existing NPU renderer with their actual extra NPU checks counted.

The physical-output observer verifies the controlled blue, green and red changes.
Mouse, keyboard, relative pointer movement, confinement and two reacquisitions
pass. Primary topology, ordinary Windows state and owned cursor state are restored.
The parent coordinator and native admission controller both complete successfully.

The first physical green observation follows the mouse receipt by 10.2468 seconds;
the first red observation follows the keyboard receipt by 0.1271 seconds. These
two events include observation cadence and do not establish p95 response latency
or panel photon timing. The previous worker's separate trial observed a 27.2093 s
mouse response; these trials are not a matched performance experiment. Usable
desktop responsiveness remains unqualified. The exact QPC derivation is retained
in [controlled-response-analysis.json](controlled-response-analysis.json).

The presenter issues 92 GPU copy commands, including 46 validation readbacks. The
independent physical observer adds 140 measurement copies. These counters do not
measure residual GPU time, bandwidth or power. Physical display scanout remains.

Unleased default Direct3D application creation still returns `887a0004` for both
tested feature lists. Normal application rendering, browser/video/game coverage,
matched Tahoma benefit and a usable install/on/off switch remain open. This is a
finite integration qualification, not full desktop offload. The earlier renderer's
thirty-minute result does not qualify this revision for that duration.

Raw reports and source snapshots retain their original hashes. Model blobs,
executables and captured desktop pixels are excluded. The legacy
`input_forwarding_tested` field remains false in the raw controller report;
the explicit `normal_windows_input_tested`, mouse/keyboard receipts and independent
physical observations record the tested Windows input path.
