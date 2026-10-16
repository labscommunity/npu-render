# Combined rectangle and complex-shader desktop trial

Live v50 passes with worker v14: 1,214 parent acknowledgments and matching NPU child
receipts, 56 exact physical submissions, controlled mouse/keyboard input, and full
restoration. One normally started non-admin default-adapter application shares
the NPU worker with DWM, executes eight verified operations, and exits before DWM
continues rendering. There is one provider generation, no fault and no retry.
Memory integrity remains enabled. This is a finite desktop trial, not full
desktop offload, arbitrary application coverage, or a Tahoma benefit.

The trial exercises 25 complex-shader draws and 149 rectangle draws: 57 direct
kernel executions and 92 retained NPU crops. Each component imports its model
once. It also executes 575 program draws, including 99 point-program draws, 143
shared geometry draws, and four long-shader draws. The bootstrap observes 69
frames with 48 distinct hashes; the physical presenter submits 56 frames with
38 distinct published hashes. These counts do not establish a sustained frame
rate for an animated desktop.

The controlled mouse event takes 2,359.834 ms to publish its matching frame and
2,488.121 ms to complete the independent physical-output pixel read. The keyboard
event takes 49.038 ms to publish and 175.981 ms to complete readback. Before mouse
publication, serialized child requests occupy 1,688.987 ms, with 670.847 ms
outside those intervals. The two events are not p95 measurements or a matched
comparison with earlier trials. Responsiveness remains inadequate.

The first registration attempt, v38, fails before graphics execution while the
ASUS nonsecure screen saver is active. Admission requires the Default input
desktop. The subsequent observer confirms the saver, and ordinary dismissal
requests fail. A bounded helper verifies the saver process identity and closes
that process without changing security or screen-saver settings. Registration
v39 passes after moving readiness before registration and keeping the observer
active through the desktop trial. Both attempts and their cleanup are retained.

The desktop itself passes, but its first post-run presentation analysis fails.
It incorrectly requires the displayed frame to match the observer's latest
submitted-source label. For the mouse event, submission nine is first observed
while display statistics still describe eight; the next observation labels
submission ten while the displayed counter correctly reports nine. The revised
analysis joins the displayed counter to the uniquely recorded submitted count,
preserving status, media-counter, synchronization and timing checks. Twelve local
positive and negative cases pass, and analysis of the retained live evidence
passes locally and on Delta. No desktop rerun or image tolerance is used.

Microsoft distinguishes the count of Present calls from displayed-frame
statistics; they need not advance together. The revised join follows that
distinction. [GetLastPresentCount](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-getlastpresentcount),
[DXGI_FRAME_STATISTICS](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics).
Display statistics and independent pixel reads remain separate observations;
neither measures panel photons.

The archive pins 221 source and evidence artifacts. It preserves the failed
registration, failed overall analysis coordinator, diagnostic traceback, and
corrected analysis. Private pixels, models, caches and compiled binaries are
excluded. The [worker archive](../native-point-rectangle-worker-v1/README.md)
records the preceding 972-request replay. The physical GPU still performs the
presentation copies and scanout; remaining GPU cost and matched Tahoma
performance have not been measured for this renderer.
