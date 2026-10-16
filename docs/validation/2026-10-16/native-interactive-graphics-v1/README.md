# Automatic graphics admission from the ordinary interactive session

Registered qualification v36 passes six clears and two indexed draws from a
normally started, non-elevated process in Windows session 1. The broker and worker
run in session 0. There is no suspended start or preauthorized application PID.
The registered Direct3D 11 runtime uses real Windows allocations; every expected
pixel matches. Eight parent NPU receipts, eight acknowledgments and eight isolated
child receipts agree. The fixture records zero CPU/GPU rendering executions.

Automatic admission verifies the actual pipe client PID, process creation time,
session, account, protocol signature and nonce. The broker creates one finite
scope-three lease. Its publication and lease mappings disappear after the client
exits. Owned process and token access lists restore exactly, the scheduled task is
removed, driver removal passes and memory integrity remains enabled.

Worker v10 changes only the pipe-access policy relative to qualified worker v9.
All other source files and linked graphics libraries match. Interactive access is
an explicit worker option; ordinary clients receive data, read-attribute and
synchronization rights, while administrators and SYSTEM retain server access.
The UMD changes its requested pipe rights and incorporates the previously
qualified interactive lease/admission code. Its three native contract tests pass.
The package stages and removes successfully before graphics testing.

The first graphics attempt, v35, fails before submitting any rendering. Its
harness incorrectly assumes that `CreateFile` with access bit four tests the
right to create a server instance. An isolated experiment reproduces that client
open succeeding under both restricted and full-control access lists. An actual
`CreateNamedPipe` duplex call fails with access denied under the restricted list
and succeeds with the full-control list, with spare instance capacity in both
cases. Diagnostic v1 also makes an invalid control assertion about changing the
pipe's open mode; v2 removes that incomparable case and passes. The corrected
graphics harness uses actual server creation and observes access denied.
All failed reports and their cleanup evidence are retained.

This follows Microsoft's distinction between opening a client endpoint and
creating an additional server instance; the latter requires
`FILE_CREATE_PIPE_INSTANCE`. Individual rights avoid granting that right through
generic write access. [Named pipe security and access rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).

The fixture explicitly selects the registered adapter. This pass does not
establish default-adapter routing, simultaneous DWM/application rendering,
lease renewal, browser/video/game coverage or existing-device migration.
There is no live desktop or Tahoma measurement in this archive.

Worker SHA-256:
`6d53376847d257c8ffa05bdc73b9319be87acf8ffb33a2f984f62dca9a1954b0`.
Installed UMD SHA-256:
`5f69fc41501fce6bde16ac4be3c91b4c7e689eb511985593d4b8d414c42f122c`.
Package version: `0.0.1319.0`. Broker v7 is unchanged from its earlier component
qualification. The archive pins 148 source/evidence artifacts. Only the three
changed UMD files are duplicated here; its complete source manifest and build
report pin the unchanged files from automatic UMD v1. The transport preparation
script is reconciled to the executed worker's conditional policy log; the archived
worker source and target build manifest are authoritative.
