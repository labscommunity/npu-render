# Native Windows registered adapter executes on Delta's NPU

Delta's ordinary Direct3D 11 runtime successfully created a device on the installed
NPU Render diagnostic adapter. Six clears and two indexed draws passed exact pixel
readback using real Windows allocation/context callbacks. The separate worker
recorded eight NPU executions and eight acknowledged deliveries. The driver
reported zero CPU/GPU provider executions and no leaked allocations, contexts or
cleanup errors. No window was presented and the physical desktop was not moved.

The trial ran on Windows 11 with memory integrity running before and afterward.
It used Delta's already-enabled test-signing mode; boot settings were unchanged.
The signed render-only kernel binary was unchanged from the qualified VM package.
The native UMD was a separately compiled variant admitted only to one controller-
owned consumer by the qualified, 120-second process-identity lease. An unleased
Direct3D process was rejected before NPU execution. Adapter selection checked the
private KMT identity, DXGI LUID, installed DLL path and signed file hash; no default
adapter, WARP or software-driver replacement was requested.

This closes the native registered-adapter boundary for these operations. It does
not establish arbitrary API/shader support, feature-level conformance, compositor
admission, screen delivery, interactive performance or a Tahoma benefit. CPU
resource decoding, transfers and kernel copies remain part of the path.

## Build and loader progression

The initial native callback test is preserved in
[native-probe-driver-v1](../native-probe-driver-v1/). The subsequent DLL test loaded
that compiled image and checked eight NPU clears through exported callback tables.
Its first build rejected Microsoft header warning C4201; the revised harness
suppressed that warning only around those headers and checked full padded
allocation bytes using callback metadata. The rejected build/source are retained.

The native driver v2 excludes direct provider objects and execution paths at
compile time. Function-level linking retains the existing shader validators
without pulling OpenVINO execution into the UMD. Both DLL and component executable
import only `D3DCOMPILER_47.dll`, `ADVAPI32.dll`, `bcrypt.dll` and `KERNEL32.dll`.
The DLL then loaded with a restricted Windows-library search and passed eight
real NPU clears without loading OpenVINO into the consumer. The callback component
passed eight clears and two indexed draws; the default guest variant separately
built and passed its CPU contracts. This does not remove OpenVINO from the NPU
worker, which remains responsible for device execution.

The patch is relative to the preserved native driver v1 snapshot. Complete source
snapshots for the DLL and registered-runtime probes, source hashes, build logs,
qualification controllers and dependency listings are included. The ordinary
Direct3D probe uses two pixel formats, an independently checked 64×64 indexed
quad, and changed constant-buffer values.

## Installation and cleanup

The first package-preparation script incorrectly queried the certificate's EKU
property and stopped before creating the package. The retained `v1b` script checks
the code-signing OID through the certificate extension. It produced the signed
package identified by `report.json` and the raw package manifest.

A staging-only transaction first established that the package and temporary
certificate trust could be removed. The subsequent installed trial completed
from 09:18:00 to 09:18:13 UTC. It created the exact diagnostic hardware ID
`ROOT\NPURENDER_NATIVE_PROBE`, version `0.0.1301.0`, and did not change display
topology. Cleanup removed the created device and its `oem19.inf` package. The
certificate had been absent from Root and TrustedPublisher; both temporary
memberships were removed. A final snapshot found only the healthy Intel Arc B390
display device, no diagnostic package, no recovery task, and neither trust entry.

A SYSTEM recovery task was armed for a bounded timeout and startup, then removed
after explicit cleanup. The timeout/startup trigger itself was **not exercised**;
this result qualifies the invoked cleanup path, not recovery from a kernel crash.
The recovery script checks the exact hardware ID, package version/INF hash and
certificate memberships owned by this transaction. It does not disable other
display devices or change code-integrity settings.

This is a private test-signed diagnostic, not a release-signed Windows driver.
Microsoft distinguishes the certificate trust used for unattended package
installation from the test-signing boot policy used for image loading. See
[installing test-signed packages](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-test-signed-driver-packages)
and [Trusted Publishers](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/trusted-publishers-certificate-store).

Seventy-seven raw artifacts were verified against remote size and SHA-256 before
compression. No private keys, model caches or driver binaries are archived here.
Delta still reported no interactive user in the final snapshot. The bounded
physical presentation watcher also expired without launching a screen probe.
