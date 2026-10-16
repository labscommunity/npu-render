# Compact filled-batch native desktop trial

Worker v18 passes registered Direct3D qualification and live v54. The desktop
trial completes 1,225 acknowledged NPU operations with one provider generation,
no faults or retries, and 57 exact physical submissions. It publishes 38 distinct
pixel hashes. Its 583 generic-program draws, 26 complex draws and 151 rectangle
draws use the qualified NPU paths. The ordinary non-admin default-adapter fixture
also passes eight operations while sharing DWM's worker.

The first mouse input reaches a matching display counter in 2,076.7295 ms and a
completed independent pixel read in 2,171.7217 ms. The keyboard result reaches a
matching display counter in 84.3813 ms and completed pixel read in 94.6706 ms.
Publication takes 2,041.5105 ms and 54.4064 ms respectively. These two events are
not a matched A/B comparison or a latency distribution. Responsiveness remains
unqualified, particularly the first mouse response.

The initial registration attempt v42 rejects the source identity before drawing:
its audit still pins the preceding program implementation. V43 verifies precisely
the three changed source hashes against worker v15, retains every other source
and linked library, and passes. Both attempts and their cleanup are retained.

The first timing analyzer also fails. It selects reads by acquisition start,
although the observer records that timestamp before its blocking
`AcquireNextFrame` call. For the keyboard event, the valid acquisition starts
0.6033 ms before input and completes 94.6706 ms afterward. The old filter skips
it and selects a later read with a zero desktop-update timestamp. The interim
analysis preserves that missing timestamp explicitly; the final analysis selects
by completed read time and retains the earlier valid observation. Eleven
synthetic checks cover the crossing interval, missing update timestamps, stale
updates, ownership, colors and invalid clocks. No captured measurements are
rewritten. The separate completion-v2 report reconciles the passing live trial
and corrected analysis while preserving the failed outer coordinator.

A zero desktop-update timestamp means that acquisition reports no new desktop
image update; pointer-only updates can carry zero timestamp and accumulated
frame count. Such a timestamp is never replaced with an inferred update time.
See Microsoft's [DXGI frame information](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_outdupl_frame_info).

Topology, window placement, cursor state, access grants and temporary driver
installation are restored. Memory integrity remains enabled. The 224-file
manifest pins sources and evidence and excludes private pixel captures, model
blobs, caches and binaries. Physical GPU copies and scanout remain; GPU relief
and Tahoma performance have not been measured by this native desktop trial.
Full desktop offload remains unfinished.
