# Native UMD callback connection to Delta's NPU

A separately compiled native diagnostic variant passed eight clear operations
and two indexed draws through the local NPU worker on Delta. The independent
pixel checks passed, including mip isolation, shared-resource clears, changed
shader constants and rejection of invalid operations without altering pixels.
There were ten NPU execution receipts and ten delivery acknowledgments, with
zero CPU/GPU provider executions and no live allocations, contexts or cleanup
errors after the test.

The consumer is admitted only by the bounded process-identity lease qualified in
[native-admission-v1](../native-admission-v1/). An unleased process was rejected
before querying the adapter or dispatching a provider. The controller verifies
its worker and consumer binaries against clean-build hashes, launches the
consumer suspended, publishes its exact process identity, and then resumes it.
The lease expires after 120 seconds and binds the worker and controller lifetimes.
The native path accepts only NPU operation; ordinary guest targets retain their
existing admission guards.

This qualification compiles the driver callback source into the test executable
and emulates Windows allocation/copy callbacks. It does **not** load the separately
built DLL, exercise an installed adapter or migrate the desktop. The compiled
DLL is an artifact for subsequent loader qualification. It was not installed,
signed or registered here. The restricted indexed command path also does not
claim arbitrary shader, graphics API or video-decoder support.

Nine raw artifacts were verified against their remote SHA-256 and size. The
patch is relative to the production-source snapshot at `1064c8b`; source and
binary hashes, build logs and the complete qualification controller are retained.
No production runtime defaults, driver registrations, certificates, boot settings
or display topology changed in this experiment.
