# Explicit physical-GPU desktop control

The ninety-second routing trial passes: Windows assigns the indirect-display
swap chain to Delta's Intel GPU (`8086:B080`), and that adapter produces a
1024×768 frame readback. There are zero NPU rendering receipts, acknowledgments,
provider generations, faults or retries. Driver removal, process access-control
restoration and the memory-integrity check pass.

The selector requires one matching hardware adapter and an exact LUID throughout
selection, device preparation and swap-chain assignment. It rejects ambiguous,
software, remote and mismatched adapters. The standalone probe passes eleven CPU
selection contracts, actual device inspection, a wrong-device rejection and four
exact GPU clear-image readbacks. The indirect driver builds in four configurations
(native/guest × GPU/paired), with five CTest suites passing in each. Fifteen
synthetic evidence checks qualify the route validator separately from hardware.

This is the first GPU route control, not a matched desktop benchmark. The trial
uses an extended indirect output; it does not make it primary, bridge it to the
physical screen, run repeated input, measure GPU utilization or execute Tahoma.
The next comparison must keep the NPU trial's logical resolution, content,
physical presenter and observation costs. Windows treats the preferred renderer
as a preference, so the validator checks the adapter actually assigned, as
required by the [IddCx render-adapter contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxadaptersetrenderadapter).

Earlier coordinator failures remain in the archive. The first selection
controller lost the short-lived child's exit status despite successful stdout;
its replacement holds an explicit process handle and records the exit code.
The first driver-build coordinator rejected that predecessor, and the second
reached its predecessor-completion check too early. Both stopped before building.
The third build and routing coordinators pass. The [manifest](artifact-sha256.json)
covers 101 artifacts, checked against device SHA-256 receipts where applicable.
No full-offload or Tahoma-performance claim follows from this result.
