# Automatic admission across the interactive session boundary

The final component qualification passes two normally started, non-elevated
interactive consumers and five negative identity cases. The broker and dummy
worker run in session zero; consumers run in the signed-in, unlocked session one.
The broker verifies the actual named-pipe client, process creation time, account,
session, protocol signature and publication nonce before creating a finite lease.
All five deliberate request alterations are observed and rejected. This is an
admission component: it makes no graphics calls and executes no NPU operations.

Six earlier failed qualifications remain intact. They expose missing process
query/synchronization rights, pipe attribute-read access, and token-query access
when crossing the elevation boundary. The final broker uses explicit data,
attribute-read and synchronization pipe rights, without granting creation of
another pipe server instance. Its pipe has a medium integrity label, while the
administrator-owned publication and lease mappings remain read-only to the user.
The test temporarily grants the same account query-and-wait access to owned
processes and query-only access to their tokens. All original access lists are
restored byte-for-byte, all tasks are removed, and all lease/publication mappings
are absent after each case. The current report's
`owned_process_acls_restored_exactly` field covers both process and token grants
in the final harness. No token duplication or impersonation access is granted.

The final binaries are from `native-auto-interactive-v7`: consumer
`ad71638f9a12645b7c192536eae15270e3d47af7b792338694e13c3e0079b404`
and broker
`3d64a9136af00050dd3b8ed7c452f8ccd994cf5307962ebfa74d7f0b4d4bb41c`.
The extended scope-three lease contracts pass, including the finite 120-second
limit and invalid session/account shapes. This does not qualify renewal, browser
sandboxes, secure-desktop transitions or integration with the real serialized
desktop renderer. Those remain separate gates.

Windows evaluates object integrity policy in addition to discretionary access;
process-handle permission and token permission are also separate checks.
These interpretations follow Microsoft's
[mandatory integrity control](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control),
[named-pipe access](https://learn.microsoft.com/en-us/windows/win32/ipc/named-pipe-security-and-access-rights)
and [OpenProcessToken](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-openprocesstoken)
documentation. The separate recorded failures establish the observed stages;
they do not independently isolate every contribution of the final configuration.

This archive also retains the isolated replay of live v43's failed desktop request.
It fails again while preparing a 17,057-node NPU graph, with no completed
compilation event, no successful rendering receipt and no fallback. The worker
is terminated and the desktop remains unchanged. The shader-model SHA-256 is
`9590a49c80df05917626cc816f534b87f6a8df7847ae794136bd7b01592e6c98`.
The complete failed desktop packet remains on Delta; its pixels are not committed.
This evidence motivates the separate longer-shader component investigation.
