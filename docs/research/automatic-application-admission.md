# Automatic application admission: next integration boundary

Updated 2026-10-16. A normally started, non-admin default-adapter fixture now
shares DWM's NPU worker and passes eight correlated graphics operations in
[live v51](../validation/2026-10-16/native-integer-rectangle-live-v1/README.md).
The fixture's process and token access changes are restored, and DWM continues
rendering after the consumer exits. This qualifies finite automatic admission;
continuous application use, renewal, revocation and multiple concurrent clients
remain open.

The earlier live v41 failed ordinary default-adapter creation because the UMD
required a valid process-specific admission record. At that point only DWM and
the indirect host received automatic records; a separate fixture used a
suspended consumer and manually published lease. The component results below
trace how the normally started interactive fixture became supported.

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

That earlier test explicitly selects the registered adapter and uses elevated same-session
processes in session zero. It does not qualify ordinary interactive applications.
The subsequent interactive-consumer scope verifies the consumer's actual SID,
session and process creation time while the controller and worker remain in
session zero. It reuses the central desktop worker and has passed standard-user
access and finite cleanup in the linked live trial.

The current v7 broker accepts one connection, retains its lease until that
consumer exits, and then removes its mappings. Interactive leases have a
120-second maximum; the UMD's `Admission` object keeps an immutable copy and
checks that deadline on subsequent exchanges. Extending a broker's mapping alone
cannot renew that existing object. The next implementation must therefore cover
both broker lifetime and UMD refresh, retain the pinned process/worker/session
identity, reject stale or revoked generations, and bound each refresh attempt.
It must qualify expiry during use, broker/worker exit, session locking, PID reuse,
concurrent clients and exact cleanup before extending the live application test.
The final scheduler must serialize NPU submissions rather than independently
importing and running per-application providers. Browser sandbox tokens, child
processes, graphics capabilities, application restart/device recreation and
explicit preservation of Tahoma's GPU selection require separate qualification.
No automatic migration or existing-device relocation is claimed yet.
