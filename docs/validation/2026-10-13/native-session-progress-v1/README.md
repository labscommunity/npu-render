# Native desktop session admission experiments

The native session candidate passes a clean Windows build, 32 CPU/admission
suites, and ten NPU-backed UMD component operations (eight clears, two draws).
These operations use emulated runtime allocation callbacks in an owned process
in session 0. The separate installed-adapter proof remains
[the preceding native trial](../native-windows-adapter-v1/).

Full physical desktop offload is not achieved. These experiments do not execute
code in DWM or WUDFHost, deliver a physical frame, or measure Tahoma performance.
The candidate changes remain isolated experiments; production retains the prior
same-session native diagnostic lease.

## Qualified boundaries

- A 224-byte global admission record binds the controller, worker and consumer
  to their individual PIDs, creation times and sessions, plus the consumer's
  actual token SID. Its ordinary diagnostic scope expires after two minutes.
  The compositor scope additionally requires the exact DWM image and virtual
  account, a signed-in unlocked user session and the Default input desktop.
  Its maximum lifetime is fifteen minutes. Thirty-five shape cases, eleven
  ACL cases and six separate-process cases pass. The successful live consumer
  was in the same session as its controller; the cross-session record checks
  are synthetic, not proof of cross-session execution.
- A worker variant pins the observed compositor PID, lifetime, image, session
  and SID and exposes an additional pipe ACL entry for that identity. Five
  invalid identity cases reject before provider execution. Three ordinary
  client processes receive exact NPU results and acknowledge delivery; three
  negative connection cases pass. DWM itself does not connect in this trial.
- Read-only Authz checks use the actual DWM and WUDFHost tokens. Both are denied
  by the existing worker pipe DACL and an owned probe process's default DACL
  for `PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`. Explicit entries for
  the observed identities pass both DACL calculations. Neither check changes
  a live object's permissions or tests mandatory integrity or an actual peer
  connection. The process descriptor belongs to the inspection executable,
  not a running production controller or worker.
- Delta's inspected WUDFHost runs as Local Service in session 0; DWM runs as
  `S-1-5-90-0-1` in console session 1. The inspected tokens report zero restricted
  SIDs. The earlier Windows session snapshot showed no signed-in user; the
  admission test also reports the console unavailable for an unlocked desktop
  trial.

The access failure is an integration finding, not a Tahoma performance diagnosis.
The next account-level trial must demonstrate real NPU requests from a separate
account with bounded permissions on owned processes. Native indirect-display
bootstrap, DWM routing, physical presentation, input and restoration remain.

## Evidence

`raw-manifest.json` records 50 artifacts whose sizes and SHA-256 values were
verified against Delta before archival. Raw files use deterministic gzip.
Standalone sources, source hashes, build/qualification scripts and patches are
included. The failed admission-v1 build (stale function calls) and peer-access-v1
build (incorrect C++ path escaping) are preserved separately from their passing
corrections. No failed build is treated as executed code.

The DWM worker patch applies to the isolated `native-render-profile-v2` source.
The session driver patch applies to `native-adapter-promoted-v1`. This preserves
the exact experiment bases without claiming that their performance variants have
been promoted to the repository runtime.

Microsoft documents that [UMDF hosts normally use LocalService](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/umdf-driver-host-process)
and that [opening a process checks its DACL for the requested rights](https://learn.microsoft.com/en-us/windows/win32/procthread/process-security-and-access-rights).
Those contracts motivate the tests; the archived hardware observations establish
the findings for this machine and these identities.
