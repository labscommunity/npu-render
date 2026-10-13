# Native renderer and indirect-display package staging

Both signed packages from [the native routing candidate](../native-routing-progress-v1/)
staged successfully on Delta and were removed afterward. The renderer version
`0.0.1302.0` published as `oem19.inf`; the indirect-display version `0.0.1301.0`
published as `oem20.inf`. Removal verified exact INF hashes and versions. Both
temporary certificate memberships were restored, the recovery task was removed,
and the existing physical display remained healthy. Memory integrity was running
before and after the transaction.

This was **staging only**: no display devices were created, neither new driver was
loaded, and no topology, boot configuration or Tahoma workload was changed. It is
not a desktop-offload result.

A SYSTEM recovery task was armed before temporary trust or driver-store changes.
Normal explicit cleanup was exercised successfully; the watchdog's delayed or
startup trigger was not exercised. Recovery tries the indirect package before
the renderer and records independent cleanup failures so one failed removal does
not skip the remaining attempts. It only unregisters the recovery task after
all checks pass.

The first preflight used a nested PowerShell JSON array during hash inspection and
rejected before creating the transaction directory, task or certificate trust.
That failure and the corrected script are preserved. `raw-manifest.json` records
thirteen artifacts whose sizes and hashes were verified against Delta before
deterministic gzip archival.
