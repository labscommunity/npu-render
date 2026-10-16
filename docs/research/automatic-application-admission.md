# Automatic application admission: next integration boundary

Updated 2026-10-16. Ordinary default-adapter creation still fails in live v41.
The native adapter is selected, but its UMD requires a valid process-specific
admission record. The current controller supplies records for DWM and the
indirect driver host. The separate successful application fixture creates its
consumer suspended, publishes a lease for that known PID, then resumes it.
That proves default-adapter rendering with manual admission, not automatic
admission of applications started normally.

The implementation boundary is `indexed_exchange_configured()` in the qualified
UMD's `indexed_exchange.hpp`. It calls `native_probe::read_mapping()` and verifies
the record. Both device admission and subsequent exchanges enforce the lease.
The v3 lease pins controller, worker and consumer PID, creation time and session,
consumer SID, deadline and private pipe suffix. Scope zero requires controller,
worker and consumer to share a session. The live desktop worker uses session-zero
compositor and indirect-host scopes instead.

The first bounded component adds an on-demand handshake before the existing
lease check. A normally started consumer discovers an administrator-owned,
read-only per-session broker record, verifies the broker process identity, and
connects to its private local pipe. The broker verifies the actual pipe client
PID, creation time, session and SID before creating the existing per-process
lease. Client-supplied identity alone must not authorize a lease. The component
qualifies normal process startup, malformed/spoofed request rejection,
bounded waits, stopped-broker rejection and cleanup. It changes neither driver
registration nor the live desktop until those checks pass.

Windows exposes the pipe's actual client and server process IDs through
[GetNamedPipeClientProcessId](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeclientprocessid)
and [GetNamedPipeServerProcessId](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-getnamedpipeserverprocessid).
Pipe DACLs should grant only the rights needed; generic write also includes
creating pipe instances, as described in
[Named Pipe Security and Access Rights](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights).
The client should bound impersonation to identification through the SQOS flags
documented for [CreateFile](https://learn.microsoft.com/en-us/windows/win32/api/fileapi/nf-fileapi-createfilew).

The [component qualification](../validation/2026-10-16/native-automatic-admission-v1/README.md)
passes two normally started consumers, four spoofed requests, an unresponsive
broker deadline and cleanup. This uses elevated same-session owned processes
and a dummy worker; it makes no graphics or NPU calls. The initial compilation
and exit-code harness failures are retained alongside the successful qualification.

The [UMD integration and registered graphics qualification](../validation/2026-10-16/native-automatic-graphics-v1/README.md)
now pass. The fixture starts normally, without suspended-process lease injection,
and performs six clears and two draws through D3D11. All eight operations have
exact NPU results and acknowledgements, one provider generation, and successful
broker and driver cleanup. The first registration attempt failed on missing stage
evidence before rendering; both that failure and the corrected run are retained.

This test explicitly selects the registered adapter and uses elevated same-session
processes in session zero. It does not qualify ordinary interactive applications.
The next boundary is an explicit interactive-consumer lease scope: verify the
consumer's actual SID, session and process creation time while controller and
worker remain in session zero. Reuse the central desktop worker rather than
creating independent application providers. Qualify standard-user process access,
negative session/identity checks, finite renewal/revocation and cleanup before a
normally started default-adapter application runs on the live desktop.
The final scheduler must serialize NPU submissions rather than independently
importing and running per-application providers. Browser sandbox tokens, child
processes, graphics capabilities, application restart/device recreation and
explicit preservation of Tahoma's GPU selection require separate qualification.
No automatic migration or existing-device relocation is claimed yet.
