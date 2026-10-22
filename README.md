# npu-render

A private research prototype that renders the Windows desktop on an Intel NPU.
A D3D10 user-mode display driver records DWM's draws into frame batches, and a
separate worker process, reached over a named pipe, executes those batches on
Delta's Panther Lake NPU through OpenVINO. Pixel work runs in SHAVE kernels
compiled ahead of time on Delta and patched into a twelve-row
ScatterElementsUpdate carrier blob with a 24,496-byte code window; twelve of
the thirteen kernel blobs pass the worker's startup self-test (the blur program
does not fit). An indirect display carries the NPU
frames to the physical panel, leases bound admission and now renew in place, and
a scheduled recovery task tears down the devices and packages and restores the
ordinary display. The long-term goal is one user-facing switch that moves
desktop composition to the NPU and frees the iGPU for other work such as
Tahoma. That switch does not exist yet.

## Status

Updated 2026-10-22. This is not a product. The latest results are trial 31 and
the application probes v115 to v118 (what ordinary applications cost on the NPU
desktop, WinUI's text program and the compositor's Mica programs moved onto the
kernel, dual-source blending, and the compositor's device churn traced to a
rejected draw and a stale shader digest) in
[frame-batch-live-trials-2026-10-20.md](docs/research/frame-batch-live-trials-2026-10-20.md);
the gate framework is the ordered gates table in
[desktop-offload-plan.md](docs/research/desktop-offload-plan.md).

| Gate | Current status |
| --- | --- |
| 1. Routing | Trials 11 and 12 pass registration, admission, presentation and restoration end to end. Driver and worker derive each fallback route from one shared selection; the worker logged zero disagreements in both trials. |
| 2. Continuous rendering | Trial 12 renders the desktop on the NPU for 2,160 s: 744 frame batches, 9,363 commands, zero self-test mismatches, zero worker stderr. All 202 per-draw fallbacks are the blur blob that does not fit the code window. |
| 3. Physical presentation | 342 physically submitted frames in trial 12 (170 in trial 11). GPU 3D engine p99 was 1.18% over trial 11's 720 s sample and 0.37% over trial 12's 37 minutes; copy and video engines and the bridge's own presentation work are not separated out. |
| 4. Interaction and recovery | Controlled input, cleanup and recovery-task removal pass. DWM survives the screen saver (build v44); a worker that dies is replaced within a second and the compositor, its clients and the display continue (trial 30, tree v52); the live tools wait out a desktop switch; recovery tasks live and die with their admission (trial 25). Open: with an application window on the desktop DWM recreated its device every few seconds (probes v115 to v118) because draws the driver could not execute failed the device; two causes are fixed in tree v58 (dual-source blending, a shader-digest cache keyed by a reused address) and the remaining rejections are being traced to their check. DWM fail-fasted once at a teardown that coincided with such a re-creation (probe v117). Longest clean admission so far 40 minutes; sleep/wake remains open. |
| 5. Responsiveness | Not met. 2.5 verified updates/s in trial 11 and 2.4 in trial 12 against a 30 Hz, 300-update stimulus at 1024x768; per-flush wall p50 265 to 281 ms; mouse sent-to-display median 94 ms and p95 1,497 ms in trial 11. |
| 6. Application coverage | Ordinary processes are admitted automatically (explorer, StartMenuExperienceHost, TextInputHost, Notepad, ClickToDo; trials 24 and probes v115 to v118) and served with zero request failures. Every WinUI frame used to pay a 2 to 4 MB per-draw request for the text program (byte-buffer `ld` plus palette); since bundle v19 it runs on the kernel and a Notepad frame costs its batch alone (p50 31 ms). The compositor's Mica backdrop programs (seven) and its dual-source caption text joined the inventory (bundles v20, v21). WPF (D3D9Ex) and Edge never open our adapter and are composited from their own surfaces. Still per-draw: minified textures over the span budget and the blur program (blob 16, 4.4 KB over the code window). Pixel correctness of application programs is checked on the device through their fixtures, not yet by replaying application captures. |
| 7. Tahoma benefit | Not measured on the native desktop. Earlier experiments regressed. The GPU 3D engine sample shows composition left that engine; it is not a Tahoma measurement. |
| 8. Usable switch | `npu-render desktop install --prepare` packages, signs and registers a build; `enable` generates, uploads and launches a stimulus-free admission with the resident broker; `status` follows it; `disable` ends it. All four verbs have run on Delta (`disable` first in trial 31); the pins follow the latest qualified build. See [desktop-switch-design-2026-10-21.md](docs/research/desktop-switch-design-2026-10-21.md). |

Correctness on the kernel path is checked on the device: every blob's fixtures
are compared pixel for pixel against the host reference, and the trial-2 capture
replays through the batch path within tolerance of that reference. Every batch
command the kernel cannot run has a qualified per-draw fallback, which the worker
reports rather than hides. Performance is the open gate.

## Architecture

The driver records draws and clears with fully resolved state instead of
executing them, flushes at Windows ordering boundaries (Present, Flush, staging
maps, CPU writes to sampled resources), and keeps a host shadow of every private
surface with a version counter. The worker keeps resident copies keyed by
backing identity and version, runs each 128x128 tile through the frame kernel
(vertex shading, coverage, sampling, the pixel program, blending), twelve tiles
per inference request, and returns the modified tiles. Programs the kernel
cannot execute take the per-draw routes inside the batch, in order. See
[frame-batch-renderer.md](docs/research/frame-batch-renderer.md) and
[frame-kernel-native-shaders-2026-10-20.md](docs/research/frame-kernel-native-shaders-2026-10-20.md).

- `native/windows/registered_lab`: the registered D3D10 UMD, the NPU worker, batch planner and protocol, host reference kernel and contract tests.
- `native/windows/ptl_kernels`: SHAVE kernel sources; `frame_core.h` is compiled both into the device kernel and into the host reference (blobs without a fetching program define `NR_FRAME_NO_LD`).
- `native/`: the C ABI, OpenVINO providers and the restricted Weston adapter from the earlier compositor work.
- `src/npu_render`: the Python runtime and CLI (backend selection, diagnostics, the software live window, carrier tooling).
- `tools/` and `tools/delta/`: build, qualification, replay and trial-chain generation; `tools/delta/trial-chain` holds the currently emitted chain.
- `docs/research` and `docs/validation`: reports and raw evidence (see below).

## Working on it

Python tooling runs through uv. Install the commit hooks once per checkout.

```sh
uv sync --frozen
npm ci && npm run hooks:install
uv run pytest
uv run pytest tools/delta/test_native_lease_renewal.py
uv run ruff check .
uv run ruff format --check .
```

The native tree is CMake. `uv run --group native python tools/build_native.py`
builds the C ABI and its tests under `build/native` against the pinned OpenVINO
wheel. The Windows driver and worker configure from
`native/windows/registered_lab` (MSVC, the pinned OpenVINO SDK and WDK headers);
its contract tests register with ctest, and `tools/build_registered_lab.py`
does a clean build and test without installing or signing anything. On Delta,
`tools/delta/build_native_tree.ps1` builds the same tree into a numbered build.

Delta trials are generated, not hand-edited. `tools/delta/make_trial_chain.py`
takes the last qualified registration, admission, bootstrap, live and recovery
scripts and emits the next numbered generation pinned to a build, driver package,
version and frame-blob bundle, asserting every substitution so a drifted source
fails loudly. A chain runs package signing, registration qualification, desktop
admission with lease renewal, live presentation with the controlled stimulus,
and recovery; its manifest and scripts are kept under `tools/delta/trial-chain`.

The kernel bundle: `tools/frame_blobs.py` emits per-blob headers, fixtures and a
single-file SHAVE source for each blob from
`native/windows/ptl_kernels/frame_programs.h`. The blobs are compiled on Delta
with the locally supplied, pinned SHAVE toolchain that
`tools/build_ptl_kernel.py` expects, patched into the twelve-row carrier
(`tools/delta/create_frame_carrier.py`) and qualified fixture by fixture on the
NPU with `tools/delta/qualify_frame_kernel.py`. The toolchain is never committed
or redistributed; neither are compiled blobs, carriers or device caches.

## Evidence and history

Reports live in `docs/research`: the acceptance plan, the frame-batch trials,
the switch design, the renderer and kernel notes, and the earlier
first-principles, compositor and coverage studies. Raw evidence for trials
through 2026-10-20 lives in `docs/validation/<date>/<trial>`; the frame-batch
trials' raw evidence (captures, logs, GPU samples) stays on Delta, and the
trials report names each transaction. Older measurements are preserved as
written; new results go in new reports.

Previous versions of this README carried a chronological log of every October
trial. That log is superseded by `docs/research` and remains in git history.

## Constraints

- The repository is private. Do not publish packages, artifacts or the repository.
- Every commit follows Conventional Commits 1.0.0; hooks and CI enforce it, with no bypass. See [CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).
- Do not commit model weights, compiled device caches, the SHAVE toolchain, credentials, private machine addresses or virtual environments.
- Never present CPU checks or graph-call latency as proof of NPU execution, display frame delivery or improved Tahoma performance.
- Do not hide device fallback or claim unsupported operations migrated. A failed replacement leaves the active renderer in place.
- Device changes are verified on real hardware where available; unavailable hardware is recorded explicitly.
