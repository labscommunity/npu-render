# Installed Windows bundle with loaded-driver reporting

The private [CI run for ee4d711](https://github.com/t8/npu-render/actions/runs/37549997956)
passed Conventional Commits, Linux/Windows tests and native builds, and all five
Weston CPU integration scenarios. Its [Windows artifact](https://github.com/t8/npu-render/actions/runs/37549997956/artifacts/11452810528)
was downloaded and checked locally and on Delta before installation.

- Source: `ee4d711e00cf5b5c58ff4ad6802952f5f4953c01`.
- ZIP SHA-256: `f8205d1c1399aab48705599dc2ec7290b26977796ff65495b4e20ead2089f6ef`.
- All 29 manifest entries matched; the archive contained no additional payloads.
- Installed under `%LOCALAPPDATA%\npu-render\builds\ci-ee4d711\npu-render`.
- The existing `ci-6f35f95` build and previous shortcut were retained.

The exact packaged executable passed full-shape GPU/NPU pixel qualification at
1280×720 and 1920×1080, within one byte, with
`--validate-only --expected-npu-driver 1004724`. Child `PATH` contained only the
bundle's runtime and Windows directories. This establishes that the bundle does
not depend on the development Python environment's runtime DLLs.

The Start-menu **NPU Render Prototype** shortcut now launches Windows `cmd.exe`
with only this version's `run.cmd`. Inspection found stale `/c` arguments naming
the older `ci-97a2491` build alongside a `ci-6f35f95` batch-file target. The shortcut
target and arguments were replaced together, then read back. A separate invocation
through `cmd.exe` and the current `run.cmd` passed pixel qualification. The
shortcut itself was not interactively clicked in this capture.

The runtime still reports loaded NPU driver **1004724**. Package 32.0.100.5540 is
installed but pending an approved system restart; see the
[activation evidence](../driver-activation/README.md). These are not new-driver
results. The installed application now reports its own runtime and loaded driver
in every successful JSON report.

All qualification runs were in session zero with no frame submissions. Their
default initial-present field is not evidence of a successful presentation.
The interactive desktop recheck produced no report (scheduled task result 1),
and its temporary tasks were removed. The most recent successful desktop
inspection remains the earlier screen-saver capture. Visible presentation,
scanout timing and the native Tahoma contention comparison remain outstanding.

`manifest.json` hashes the unchanged raw captures stored as gzip. Adjacent JSON
files normalize encoding only. No compiled binaries or private machine addresses
are included here.
