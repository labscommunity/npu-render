# Integer rectangle desktop trial

Live v51 passes with worker v15: 1,163 parent acknowledgments and matching NPU
child receipts, 56 exact physical submissions, controlled input, and full
restoration. A normally started non-admin default-adapter application shares
the worker with DWM and passes eight operations. Memory integrity remains
enabled. One provider generation completes with no fault or retry.

The trial executes 549 program draws, including 93 point-program draws, 26
complex-shader draws and 147 rectangle draws. Of the rectangles, 59 use the
direct kernel and 88 retain the existing NPU crop path. The physical presenter
receives 38 distinct published hashes. These finite counts do not establish
the frame rate of a continuously animated desktop.

The mouse response takes 2,456.411 ms to publish, 2,502.717 ms to match display
statistics, and 2,544.636 ms to complete an independent physical-output pixel
read. The keyboard response takes 52.841 ms, 86.894 ms and 125.073 ms respectively.
These are two events, not a latency distribution or panel photon measurements.
The preceding live trial observed 2,488.121 ms and 175.981 ms for the two pixel
reads. Different live request counts and uncontrolled cross-run conditions
prevent attributing these differences to the integer kernel.

Before the mouse frame is published, child rendering intervals occupy
1,685.570 ms, with 770.841 ms outside them. The component improvement therefore
does not resolve whole-desktop latency. General shader batching and host
orchestration remain targets for measurement.

Registered D3D, live desktop execution and the counter-based presentation
analysis all pass in this run. The archive pins 161 source and evidence files;
the [worker replay archive](../native-integer-rectangle-worker-v1/README.md)
records all 972 preserved outputs. Private pixels, model blobs, caches and
compiled binaries are excluded.

Full desktop offload remains unfinished. The physical GPU still performs
presentation copies and scanout. Sustained responsiveness, continuous arbitrary
application admission, GPU relief and a matched Tahoma benefit remain unqualified.
