# Native Windows client qualification

The local C++ client is now included in the repository's normal registered-renderer
build. A clean Windows build from these sources passes **30 CPU test suites**,
including the new **15-case transport contract**. The same built client then
connects three separate processes to Delta's NPU worker: all three receive exact
pixels and rendering receipts, and the worker records three delivery acknowledgments.
An earlier isolated build passes the same hardware checks. These are repeated
requests from the first saved desktop draw, not another full desktop replay.

The client retains a supplied worker process handle, checks that the process is
alive and verifies the named-pipe server's process ID before sending data. The
controller must establish the worker's identity; discovering a PID from an
arbitrary pipe is not authorization. Process-handle lifetime and access rights
follow Microsoft's [process security contract](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights).
The diagnostic probe accepts the PID of its controller-owned worker and opens
only synchronization and limited-query access.

The complete NPU response decoder checks the reply and its receipt before the
client acknowledges delivery. A failed exchange permanently invalidates that
client. Deadline and cancellation paths drain pending overlapped I/O before
releasing its storage. Synthetic local peers verify correct delivery; wrong
nonce and device; reserved, excess-coverage, invalid-byte-count and zero-call
receipts; truncated pixels; provider rejection; pending-read timeout and
cancellation; and four admission checks. Invalid replies receive no acknowledgment.
These synthetic replies do not execute any rendering device.

Physical NPU qualification separately verifies wrong-server rejection,
cancellation before connection and unavailable-endpoint timeout. Each failed
client rejects reuse; the worker then serves the three valid processes and exits
successfully. Both builds use the previously qualified local worker and preserve
its executable identity in the referenced build receipt. No driver is installed.

A separate permissions experiment reproduces the worker's protected user/SYSTEM
pipe access list in a temporary pipe. The creating process has high integrity.
A duplicated token with the same user identity and medium integrity can open
the pipe; a low-integrity token receives access denied (Win32 error 5). The test
verifies the effective thread token and restores it afterward. It changes no
system security configuration. This tests reduced thread tokens, not a separate
application process, the full native client, an AppContainer or DWM's account.
Microsoft documents the distinction between [pipe access permissions](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)
and [mandatory integrity checks](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control).

The read-only label snapshot reports no explicit label. V2 uses the documented
file-object category for named pipes; the initial kernel-object query is retained
as superseded evidence. Neither label inspection alone nor the thread-token test
establishes access by Windows' separate compositor identity.

Fifty-one remote artifacts were checked against SHA-256 and size receipts and
preserved losslessly under `raw/`. Source manifests identify both clean builds;
the client and contract test match the promoted files byte for byte. Local
validation also passes 131 Python tests and both Ruff checks. GitHub CI is
reported separately after the commit rather than inferred from local results.

Existing guest-only driver admission and graphics routing remain unchanged.
Physical adapter admission, compositor-account access, frame presentation,
input routing and recovery remain unqualified. Delta still had no signed-in
user at the latest session check. No physical desktop migration or Tahoma
performance improvement is claimed.
