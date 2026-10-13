# Lease-gated native UMD diagnostics

The native diagnostic candidate records 65 trace entries while completing ten
exact NPU operations and ten delivery acknowledgments. An unleased process writes
no diagnostic files. The owned trace directory is removed afterward. The default
guest UMD and its CPU component contract also build and pass.

Previously the UMD's bounded logging was enabled only in the owned VM, leaving
native DWM admission failures without that evidence. The candidate permits logging
while a native admission object remains valid and preserves the existing guest
path. The native destination is a controller-created protected directory under
ProgramData; the driver does not create it. Process logs retain the existing
4 MiB/4096-line bounds. Rejected shader and draw capture use the same admission
and destination, but their successful capture paths were not exercised here.
No captured desktop content is part of this trial: the trace is from synthetic
clear/draw fixtures in an owned process.

The first build rejected incorrectly escaped C++ path literals. The corrected
build uses raw string literals and preserves the guest's cached enablement check.
Both source patches and the failed build log are archived. The final run's trace
contains 5,938 bytes; its hash and the complete raw receipt are retained.

Nineteen artifacts were remotely size/hash verified before deterministic gzip
archival. The new DLL has not been packaged or installed. This is not DWM or
WUDFHost execution, frame delivery, a performance comparison, or full desktop
offload. The preceding signed packages and staging proof remain unchanged in
[the native routing record](../native-routing-progress-v1/) and
[pair staging record](../native-pair-stage-v1/).
