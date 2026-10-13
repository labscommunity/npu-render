# Native compositor and indirect-host routing candidate

The session-aware renderer candidate passes ten real NPU operations through its
UMD component, and the worker passes three exact ordinary-client requests while
pinning both observed Windows host identities. Nine invalid identity cases reject
before NPU execution. This is progress toward native desktop routing, not a native
desktop result: DWM and WUDFHost have not executed the candidate, and no physical
frame or Tahoma speedup is demonstrated.

## Implemented and checked

The 224-byte protocol-3 lease separates owned diagnostics, DWM and the indirect
driver host. It binds PID, creation time, account SID and process session, with a
separate interactive desktop session. The compositor scope requires the exact
DWM image/account and Default input desktop; the indirect-host scope requires
System32 WUDFHost and Local Service in session 0. Both desktop scopes require a
signed-in unlocked target session. Forty-four shape checks, eleven ACL checks
and six separate-process cases pass. Positive live drawing still uses diagnostic
scope in session 0; synthetic record validation is not cross-session rendering.

The native indirect variant waits at most ten seconds for its per-process ready
event before reading the completed host lease. It verifies admission before
framework/device startup, monitor preparation and frame consumption. The default
guest build retains its guest guard. Both variants clean-build and pass three
ownership/readback/admission suites. The guard test rejects this ordinary test
executable; actual UMDF driver entry and readiness timing remain untested.

A real Local Service diagnostic queries console session 1 successfully through
WTS, including the extended state structure. Thus lack of query permission did
not block that host-side readiness check on this machine. The console had no
signed-in user. Its temporary task, process and directory were removed.

The physical presentation probe now requires its own process to be on the
signed-in, unlocked Default console desktop, rechecks before showing its window
and during presentation, and provides a read-only session inspection. The
controller compares the console's resolved account SID with its own account.
A valid frame is rejected before window creation from the services session.
The first rejection validator encountered PowerShell's stderr wrapping; the
corrected direct process redirection confirms the exact rejection. No physical
presentation has occurred. The archived watcher snapshot is pending; the watcher
was armed from 10:43 to 11:43 UTC and had not launched its finite probe.

The renderer and indirect packages are signed using the existing laboratory key,
with no private key exported. Their DLL imports contain Windows libraries only;
OpenVINO remains in the worker. The package reports here precede
[successful staging and removal of both packages](../native-pair-stage-v1/).
Neither new driver has been loaded. The earlier
[installed single-process adapter trial](../native-windows-adapter-v1/) remains
separate evidence.

## Evidence and remaining work

Eighty-three raw artifacts were size/hash verified against Delta before archival.
Source manifests, standalone sources, build/qualification scripts, patches,
package manifests and deterministic gzip files are included. The routing patch
applies to `native-service-peer-v2`; the indirect patch applies to repository
`native/` at the `8f100fe` runtime state. These candidates are not promoted to
production.

The installer/controller still must discover the new indirect host, start and
pin the worker, publish both host leases before admission, observe the actual
selected rendering adapter and recover on failure. Physical presentation, moving
the interactive desktop, input mapping and exact topology restoration remain.
Only after that path works can a matched desktop/Tahoma experiment establish
whether useful GPU capacity has been recovered.
