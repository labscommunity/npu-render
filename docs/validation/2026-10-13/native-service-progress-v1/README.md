# NPU drawing across the Local Service account boundary

A real Local Service process completed eight clears and two indexed draws through
the native UMD component and the separate NPU worker. All ten operations returned
exact pixels and delivery acknowledgments, with no CPU or GPU provider execution.
The paired control was rejected before NPU execution with the original controller
and worker process permissions. Both processes received only query/synchronization
access for Local Service in the passing trial; their original DACL bytes were
restored afterward. Temporary scheduled tasks, owned processes and diagnostic
files were removed successfully.

This is an account boundary test in session 0. The consumer is an owned diagnostic
executable launched by Task Scheduler as `S-1-5-19`, not WUDFHost or DWM. Its UMD
allocation callbacks are emulated. No adapter was installed, no monitor topology
changed, and no physical desktop or Tahoma result is claimed. The earlier
[installed adapter proof](../native-windows-adapter-v1/) remains a separate trial.

## What changed

The worker pins the diagnostic's PID, creation time, Local Service identity,
session and exact sibling executable path. The existing compositor mode retains
its DWM image/SID checks. The controller publishes the global 224-byte consumer
lease and signals a separate readiness event after the complete record is
written. The Local Service process rejects unleased adapter access before waiting
for that signal. It exits within 110 seconds; its one-shot task has a two-minute
limit. Only newly owned directories/tasks can be used; existing names reject.

The passing permissions grant is `PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`
on the owned controller and worker process objects. It does not grant process
memory access, thread creation or process termination. This validates the access
gap found in the [read-only peer inspection](../native-session-progress-v1/).
The grant is scoped to the Local Service account, while the worker additionally
checks the actual connecting process against its pinned consumer identity.

## Preserved failures and limits

The first two controllers could not read the diagnostic's live output because
`_wfreopen_s` had opened it exclusively. The corrected executable uses an explicit
shared Windows file handle. The first corrected trial rendered and delivered all
ten results but its validator incorrectly required an empty consumer stderr.
The component intentionally logs four diagnostics for invalid-resource test
cases. The final validator requires those four exact lines; unexpected diagnostics
still fail. Earlier failed reports remain unchanged in the archive.

`raw-manifest.json` records 37 remotely hash-verified artifacts, stored using
deterministic gzip. The two clean build source manifests and source patches,
controllers and cleanup receipts are included. The final denied/granted pair uses
the same `native-service-peer-v2` binaries. These experiments are not promoted to
the production runtime.

Native indirect-driver bootstrap must still identify its newly created WUDFHost
process and publish an appropriate lease before Direct3D device creation. DWM
needs its own session-aware lease. Neither requirement is satisfied merely by
running a diagnostic under the same account as the driver host. Physical output,
input, recovery and a matched Tahoma comparison remain required.
