# Native graphics and NPU textures across processes

All ten [hardware cases](qualified-final/supervisor.json) pass on Delta. A native graphics
process draws changing foreground/background pixels into two typed NT textures.
A child process opens those textures and a third output texture, maps their pages
through the GPU KMT device, creates local raw aliases and imports all three as NPU
remote tensors. The NPU composes; the parent samples the result with a native GPU
shader and compares every pixel with an independent integer oracle.

This advances the [one-process texture result](../typed-textures/README.md) to a
graphics producer and independent NPU worker. It remains a component experiment:
no registered graphics adapter, DWM selection, desktop work retirement, general
shader execution, scanout measurement or Tahoma improvement is established.

## Results

| Case | Frames | Result |
|---|---:|---|
| BGRA 64×48 | 32 | Pass |
| RGBA 320×240, creator release/reopen | 32 | Pass |
| BGRA 1280×720, creator release/reopen | 32 | Pass |
| BGRA 2880×1800, creator release/reopen | 32 | Pass |
| RGBA 2880×1800, creator release/reopen | 32 | Pass |
| Invalid row alignment | 0 | Explicit rejection |
| Worker receives mismatched texture dimensions | 0 | Rejection before NPU readiness |
| Parent terminated after NPU readiness | 0 completed before injection | Child exits; about 31 ms observed shutdown |
| Worker terminated after NPU readiness | 0 completed before injection | Parent reports failure; about 47 ms observed shutdown |
| Fresh 720p process after both faults | 32 | Pass, including release/reopen |

All positive frames have at most one byte/channel error. All normal worker exits
report three successful alias cleanups. Both graphics debug queues remain clean.
The forced process exits use OS cleanup; they do not claim graceful alias cleanup
or recovery from a wedged NPU/graphics driver. These faults occurred after readiness
and before a frame was observed complete; they are not deterministic interruption
inside an active NPU command.

The 14 earlier texture cases also pass after extracting the shared helper into
`native/windows/typed_texture.hpp`; their [regression report](regression-final/supervisor.json)
retains the new source/binary hashes. Both reports include runtime DLL hashes and
byte-exact raw stdout/stderr manifests. The worker binary SHA-256 is
`d3c4209c78aa2adb0f69e6140e8c9d21699ab592834d848fb306ff49faebf0e1`.

The initial successful qualification is also retained under `qualified/` and
`regression/`. Its helper differs only in the later writable-page guard and an
EOF newline; the exact original helper is archived as `initial-typed_texture.hpp.gz`.
The final reports above match the shipped sources.

The [private CI build](https://github.com/t8/npu-render/actions/runs/37849110664)
for `98c9d59004e915fdda625fa5bdc7ab00abea2d58` also passes Windows/Linux tests,
native builds, Weston integration and Conventional Commit checks. Its downloaded
Windows ZIP was copied to Delta, all 39 manifest entries verified, and its exact
worker executable passed 32-frame small, full-resolution BGRA and full-resolution
RGBA release/reopen cases. See [package verification](package-98c9d59/package-check.json)
and its retained raw outputs. This validates the CI artifact, separately from the
locally compiled hardware qualification. Archive SHA-256:
`a78281de298a558ee4edef166eaf43a024fb677237675491961363db632044ad`.

## Ownership, synchronization and retained work

Only six inherited resource/event/log handles enter the worker. A Windows job
with kill-on-close is assigned while the child is suspended, before it executes.
The parent drains worker logs while waiting, enforces deadlines and observes the
child process handle as well as its completion event. The fault supervisor retains
the exact child's process handle and verifies exit; it does not terminate programs
by executable name.

At frame 16 in release/reopen cases, the parent unbinds resources, completes GPU
work, releases its texture objects and closes its original NT handles. The child
still owns handles, imported textures and aliases. The parent duplicates those
child handles back and reopens the same textures. The remaining changing frames
must continue passing the oracle.

GPU completion queries precede NPU reads. Synchronous inference precedes GPU
sampling, and staging readback completes graphics consumption before the next
write. This establishes host-coordinated ownership, not native asynchronous
GPU/NPU fences. Each positive run retains 64 GPU producer draws, 32 GPU consumer
draws and 1,024 bytes of constant-buffer uploads. There are no explicit CPU pixel
uploads or NPU-output copies; oracle readbacks and any internal vendor-driver
transfers are separate costs. Timings include validation and are not display FPS.

## Windows compositor shader inventory

The [static inventory](compositor-shaders.json) records hashes, container offsets,
profiles and instruction/declaration counts from Delta's installed compositor
modules. It retains no system DLL or shader bytecode. `dwmcore.dll` contains 446
validated DXBC containers, including 428 reporting `lib_4_0`; `dcomp.dll` contains
two, and no qualifying container was found in `udwm.dll`. Some containers also
include downlevel profiles, so profile totals are not distinct shader totals.

The disassemblies include texture sampling, derivatives, conditional flow, loops,
integer operations and nonlinear arithmetic. There are 88 distinct mnemonics in
the DWM inventory **including declarations and downlevel variants**. They are not
88 independent algorithms or proof of the active desktop's exact requirements.
Static discovery does not identify which functions are linked and executed for a
particular desktop frame. A graphics driver still needs runtime shader/state
coverage; recognizing our one laboratory shader pair is insufficient.

The scanner reads system files and uses Microsoft's
[`D3DDisassemble`](https://learn.microsoft.com/en-us/windows/win32/api/d3dcompiler/nf-d3dcompiler-d3ddisassemble).
Normal UMD shader creation receives tokenized programs through
[`CreatePixelShader`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d10umddi/nc-d3d10umddi-pfnd3d10ddi_createpixelshader).
No desktop process is injected, system binary modified or driver registered.

## Reproduce

Build `nr_texture_worker` and `nr_d3d11_texture_interop` with the pinned native
environment, then run from the repository on Windows with Graphics Tools installed:

```powershell
python tools/validate_windows_texture_worker.py `
  --binary-dir build/native/Release --source-dir native `
  --output results/texture-worker --debug
python tools/validate_windows_texture_interop.py `
  --binary-dir build/native/Release --source-dir native `
  --output results/texture-regression --debug
python tools/inventory_windows_compositor_shaders.py `
  --output results/compositor-shaders.json
```

Evidence destinations must be new. Device failures, stale pixels and unavailable
NPU execution fail explicitly. The currently installed Windows desktop remains
the active renderer throughout these checks.
