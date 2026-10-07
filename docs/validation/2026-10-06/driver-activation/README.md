# Driver installation is not activation

Intel's [32.0.100.5540 package](https://www.intel.com/content/www/us/en/download/794734/intel-npu-driver-windows.html)
was downloaded, checked against its published SHA-256, and extracted without
running the self-extractor. Windows verified the installer/kernel signatures and
the Microsoft-signed catalog. The INF explicitly includes Panther Lake device
B03E. The current 4724 package was exported first (18 files, 94,337,115 bytes).
The export remains on the test host; driver binaries are not committed here.

The vendor installer ran with `--noExtras --report <log> -s`. No clean-install,
forced-process-termination or automatic-reboot option was selected. Its log
reported **exit 14: success, restart required**. The PowerShell `Process.ExitCode`
property was null after the process exited; the retained result preserves that
fact, and the exit-14 finding comes from the installer log excerpt.

Windows subsequently reported package 32.0.100.5540 and device status OK / problem
code zero. Both OpenVINO 2026.1.0 and 2026.3.1 still reported loaded
`NPU_DRIVER_VERSION=1004724`. An NPU-only PnP restart was refused because a system
reboot is pending. **No system reboot has been performed in these captures.**
Approval for a full restart is pending because it closes the user's Windows
session. The full objective and post-restart qualification remain open.

## What these measurements establish

The known-good native sampling and 97-case resize tests passed after installation,
with at most one byte of error. Shared-buffer composition passed at 720p and 1080p,
and the general sampling/resize benchmarks also passed. These are **still old
loaded-driver results**, not qualification of 5540. Similar timing before/after
package installation is not evidence that the new driver changes nothing.

The existing Windows prototype remains installed. No reboot, rollback, shader
patch, display-mode change or system compositor replacement occurred.

## Preventing false qualification

`npu-render stack` records runtime/plugin versions and advertised driver/compiler
properties without compiling a graph. `--expected-npu-driver 1005540` fails with
the complete report when the loaded version is missing or different. Core's
compiler-type property is a preference (`PREFER_PLUGIN` here), not a claim about
the final compiler used by every graph. Compiled-model inspection separately
reported `PLUGIN`; the exploratory stack captures retain both runtime versions.

The native window also records its own runtime and loaded NPU driver. Its
`--expected-npu-driver` check runs before NPU compilation and readiness. On Delta,
1004724 passed full-shape GPU/NPU pixel qualification, while 1005540 failed before
readiness and produced no successful report. The native inference harness checks
both its Python runtime and the renderer child, since their adjacent DLLs may
differ. A mismatched version cannot produce an accepted contention trial.
The harness was also exercised on Delta with expected version 1005540: it exited
with a failed report, zero trials, and no inference warmup. The matching CLI check
accepted 1004724. These are driver-gate checks, not a Tahoma benchmark.

## Direct3D capability probe

The new `nr_dxcore_probe` defaults to a runtime-independent NPU workload query.
`--d3d-ml` instead selects the Direct3D generic-ML adapter list. Both distinguish
NPU hardware identity, generic-ML support, Core compute, graphics, device creation,
and shader-model query results. They dispatch no shaders or graphs and never
substitute a GPU for a missing NPU.

Before reboot, the workload query returned no NPU. The generic-ML query listed
only the Intel GPU and Microsoft software renderer. These are retained failures
to expose an NPU through those queries in the current state, not a fundamental
hardware conclusion. Intel's package containing `npu_d3d12_umd.dll` is insufficient
evidence of usable compute shaders. Repeat both probes after verified activation.

Microsoft documents [hardware-type versus runtime attributes](https://learn.microsoft.com/en-us/windows/win32/dxcore/dxcore-adapter-attribute-guids)
and the separate [Core compute feature set](https://learn.microsoft.com/en-us/windows/win32/direct3d12/core-feature-levels).
An NPU may expose a compute driver without the Direct3D user-mode interface; generic
ML metacommands and general compute-shader support must be checked separately.

## Evidence and next checks

`manifest.json` hashes unchanged decompressed captures. Normalized JSON removes
PowerShell encoding and extracts reports after compiler diagnostics. The full
installer log remains on the host; its hash and relevant completion lines are
retained here to avoid including unrelated device enumeration. Package SHA-256:
`fba32699b699918a793fca05cfc6c607c8745772057347476ecd8c4cf7746048`.

After an approved restart, first require loaded version 1005540. Then rerun native
pixel/shared-buffer qualification, resize/sampling timing, the previously rejected
GridSample and convolution-packing cases in bounded processes, and DXCore queries.
Keep device-loss recovery and rollback available. Visible presentation and the
native Tahoma comparison additionally require an unlocked interactive desktop.
