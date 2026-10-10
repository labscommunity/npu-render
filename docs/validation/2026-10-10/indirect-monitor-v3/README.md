# Indirect monitor and runtime loading

The virtual monitor starts and is announced. Both activation tests fail their
active-path check despite successful API return codes; Windows has not assigned
a desktop swapchain to the monitor. The original display is restored.

A separate fresh-process test isolates a missing deployment dependency: the
registered UMD requires explicit runtime setup until its 16 verified runtime
DLLs are colocated. That candidate loads without the explicit setup. This is
not evidence of compositor selection or desktop offload.

[report.json](report.json) links the raw results, exact builds and limitations.
