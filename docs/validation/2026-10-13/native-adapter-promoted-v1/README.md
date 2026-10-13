# Qualified native adapter code in the regular repository build

The native UMD, bounded process-identity admission, callback component, admission
contracts and ordinary Direct3D diagnostic probe are now part of the regular
registered-driver build. The standard guest target retains its original admission.
Building these targets neither signs nor installs a driver.

A clean build from the repository's runtime sources passed all 32 Windows CPU and
admission contract suites on Delta in 249.45 seconds. Its source hashes match the
committed source inputs. The new binaries separately passed ten real NPU callback
operations and eight DLL-loaded NPU clears, with exact pixels and acknowledged
receipts. The DLL test used Windows-library-only resolution and confirmed that
OpenVINO was not loaded into the consumer. The NPU worker still uses OpenVINO.
Local verification passed 131 Python tests and Ruff check/format checks.

The preceding [registered-adapter trial](../native-windows-adapter-v1/) installed
the separately built native variant and passed normal Direct3D calls with real
Windows callbacks. This new build was checked at the callback and DLL boundaries;
it was not installed again. Those scopes remain separate in the raw evidence.
The installed trial and its temporary certificate trust were fully cleaned up.

The read-only session snapshot uses WTSEnumerateSessions and WTSQuerySessionInformation,
rather than relying only on Win32_ComputerSystem.UserName. It found no active user
session: console session 1 was connected but had no user. DWM ran in session 1
under `S-1-5-90-0-1`. The native diagnostic currently admits one owned consumer in
the controller/worker session; it does not admit that compositor identity or
implement a full desktop switch. This result does not establish screen delivery
or a Tahoma performance improvement.

Seventeen original artifacts were remotely hash/size verified and archived with
source/build/qualification scripts. Live tests used OpenVINO 2026.4.1 on Delta's
NPU; normal repository CI uses its pinned runtime and does not have NPU hardware.
