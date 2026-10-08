# Direct3D driver command execution on Delta

A restricted D3D11 command stream now executes admitted draws on Delta's NPU
through our process-local graphics-driver DLL. **Full Windows desktop offload is
not implemented.** DWM, arbitrary application rendering and the monitor path
remain on their existing routes. There is no Tahoma performance result from this
experiment.

## What ran

`nr_ddi_probe` loads an explicit DLL and calls `D3D11CreateDevice` with
`D3D_DRIVER_TYPE_SOFTWARE`. Microsoft's runtime negotiates our D3D10 user-mode
DDIs; the probe then uses ordinary texture, shader, state, draw, copy and map calls.
The driver recognizes an exact fullscreen shader pair and translates each admitted
draw to the existing OpenVINO source-over graph on the requested CPU, GPU or NPU.
It checks the compiled execution device and never selects AUTO/HETERO or silently
substitutes another provider. See the [implementation contract](../../../../native/windows/ddi/README.md).

All three backend selections passed eight draw checks each. Tests cover RGBA8 and
BGRA8, 64/132 by 48 textures, fractional alpha, and a second draw after changing a
source subrectangle. Every output channel is compared with a double-precision
source-over oracle; maximum permitted difference is one byte. Negative tests run
in separate processes: an unknown pixel shader, a shared texture request, and a
draw without required state. Each is rejected with zero inference executions.

The source-built NPU run reports eight draws/eight executions, loaded NPU driver
`1005540`, zero rejected commands in the positive case, and maximum byte error 1.
Explicit host work totals 169,344 initial/update bytes, 150,528 `CopyResource`
bytes, 301,056 inference input bytes, and 150,528 inference output bytes. CPU clears
are zero. These count our copies, not internal runtime/device memory traffic.
The test is small and offscreen; whole-process elapsed times include compilation
and are not frame-performance measurements.

## Attribution and retained evidence

The `qualified` capture uses the locally built binaries from runtime revision
`9333d37cba274ab1dee06f7b9204eb3e2af49175`. All executed driver, shader, model,
CMake and supervisor source hashes were compared with that checkout. The raw
supervisor records exact executable/runtime hashes, commands, exit codes, times
and report hashes. Twelve process checks passed: one positive and three negative
cases on each backend.

The exact 36-file private CI bundle from
[run 37807412108](https://github.com/t8/npu-render/actions/runs/37807412108) was then
installed in an isolated directory. Every manifest hash was verified locally and
on Delta before execution. The same twelve checks passed again against the
packaged binaries and bundled OpenVINO runtime. The `ci-qualified` capture records
these separate results and executable hashes. The NPU result again reports eight
executions and maximum byte error 1. Previous stable live/delegate executable
hashes match their earlier installation records, and the delegate launcher still
points at its previous build.

That CI run passed 108 Python tests, all ten Windows native CTests (including the
four DDI cases), Linux native tests, Weston headless/nested/transform/resize checks,
Ruff, and Conventional Commit validation. CPU CI is not used as NPU evidence;
the real NPU results are the two Delta captures.

Raw files retain their original encoding/bytes in deterministic gzip. The manifest
records compressed and original hashes. `provenance.json` identifies the source,
build dependencies, preliminary limitations and private CI package used for the
additional package check.

The machine was Windows 11 Pro on Core Ultra X7 358H, Arc B390 and Intel NPU, with
NPU driver 32.0.100.5540 and GPU driver 32.0.101.8724. Battery was 100% on AC. The
last boot remained October 7 at 17:34:30.500 UTC. No system driver, security, power
plan or desktop-selection changes were made, and no probe remained running after
qualification. The existing launchers are not redirected to the laboratory DLL.

## Limits and next driver gate

- `SOFTWARE` identifies the explicit DLL loading route. The admitted NPU draws
  really use OpenVINO's NPU device, but this is not a registered NPU graphics adapter.
- Feature level 10_0 selects the driver ABI. The restricted DLL does not implement
  that feature level's full conformance requirements.
- The exact shader recognizer is not a general shader compiler. Most D3D operations,
  shared resources, presentation and synchronization across processes are absent.
- Software-adapter metadata/LUID is synthetic. It must not be interpreted as the
  physical NPU identity or as proof Windows can select it for composition.
- Shared-texture rejection documents the current implementation gap. It does not
  prove that a WDDM NPU renderer is impossible.

The next substantive boundary is real WDDM shared allocations and ownership/fences,
followed by actual DWM adapter selection and the composition shader/state stream.
A host file mapping, synthetic adapter enumeration, or postprocessed desktop capture
would not establish this. The [Windows integration assessment](../../../research/windows-compositor.md)
records the primary-source requirements. These tests establish graphics-command
execution, not display delivery, retired DWM work, general GPU replacement or
improved Tahoma throughput.
