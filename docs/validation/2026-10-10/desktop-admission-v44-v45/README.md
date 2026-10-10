# Desktop admission diagnostics

The installed driver now loads with its colocated dependencies. Explorer and Start
Menu then request constant buffers, which the v45 UMD rejects. These traces are
from application processes; they do not demonstrate DWM selecting our renderer.

The indirect display still fails activation with this renderer. The finite v3d
probe restores the original display; v3e times out with incomplete evidence.
Both experimental driver packages were removed cleanly.

[report.json](report.json) records exact builds, process counts and limitations.
