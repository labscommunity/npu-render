# Registered geometry buffers

The installed driver passes 72 exact readbacks covering default, dynamic and
immutable vertex/index buffers, including discard and no-overwrite updates.
The 20-readback constant-buffer regression also passes. All allocations are
released with no rejected calls or cleanup errors. These are resource tests.

Explorer and Start Menu now reach a previously unsupported vertex shader.
Desktop activation still fails to complete before the task deadline; DWM use
and NPU desktop rendering remain unproven. Both drivers are removed cleanly.
See [report.json](report.json) for hashes, evidence scope and limitations.
