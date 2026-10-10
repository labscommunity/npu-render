# npu-render

A switchable graphics runtime that moves supported rendering operations between
NPU, GPU, and CPU execution. The objective is a single control that frees GPU
resources for workloads such as Tahoma, with application and compositor adapters
providing the integration boundary.

**Status: private prototype.** The runtime implements deterministic premultiplied
RGBA composition and an explicit backend switch. The packaged runtime passed
GPU → NPU → CPU → GPU → NPU switching on Panther Lake. It supports CPU/GPU/NPU through
its first OpenVINO provider; other providers can implement the same contracts.

The switch affects participating integrations. A restricted Weston adapter passes
Linux CPU headless/nested tests, and D3D12 shared buffers pass a real Panther Lake
NPU round trip on Windows. Native Linux NPU presentation, Windows DWM replacement,
arbitrary Direct3D/Vulkan games, and codecs remain unfinished. The earlier native
Tahoma investigation reduced NPU blend time by **33%**, but the optimized path
still made inference **4.7% slower** than a D3D12 shader in a five-round repeat
after Delta's power change restored increasing battery charge under load.
Isolation controls find a **3.3%** inference penalty from NPU blending even without
repeated uploads or presentation; adding comparable elapsed time with a host wait
does not reproduce it. The full-pipeline difference varies between experiments.
See the [power repeat and interference controls](docs/validation/2026-10-07/power-retest/README.md)
and [graph cost investigation](docs/validation/2026-10-07/slowdown/README.md).
Practical acceleration remains unproven.

A Windows experiment now hands composition of **three cooperating GDI windows**
from DWM to the NPU or GPU, hides the originals during delegation, and restores
them when switched back. All modes pass exact on-screen pixel checks. The private
bundle includes `run-delegate.cmd`: **1 DWM / 2 NPU / 3 GPU**, at a default 30
updates/s. Other applications and the outer Windows desktop stay on DWM. This is
a bounded handoff, not a system-wide switch. See the
[handoff and follow-up measurements](docs/validation/2026-10-07/desktop-delegate/README.md).

The actual handoff reduced sampled DWM activity, but NPU composition made Tahoma
**7.6% slower** than letting Windows compose these windows in five paired rounds.
The NPU tile-count and priority follow-ups also did not establish a reliable Tahoma
improvement. A direct adapter probe finds Delta's NPU through DXCore but confirms
that its installed driver rejects the tested Direct3D device-creation routes.
The [Windows integration assessment](docs/research/windows-compositor.md) describes
the additional renderer/driver work required for arbitrary existing applications.

An [autonomous retained-buffer follow-up](docs/validation/2026-10-08/retained-windows/README.md)
eliminates repeated foreground host uploads and batches placement into rectangle
copies. This reduces NPU frame processing from 15.1 to 11.7 ms. Tahoma throughput
improves 2.1% versus staged NPU in the paired median (four of five rounds), but
remains 3.1% below ordinary Windows composition in all five comparisons. Row-by-row
copies regress despite moving fewer bytes. Both retained variants remain explicit
experiments; the default is unchanged and a net Tahoma benefit remains unproven.

The [Direct3D command laboratory](native/windows/ddi/README.md) now loads our
process-local driver DLL through the ordinary D3D11 runtime and executes admitted
source-over draws on the NPU. It recognizes an exact shader pair and rejects
unsupported commands. Shared textures, presentation, a system graphics adapter,
and full desktop routing remain unimplemented; feature-level negotiation is not
conformance. The private Windows bundle includes this independently testable step;
[source and exact-package checks](docs/validation/2026-10-08/windows-ddi/README.md)
pass on Delta's CPU, GPU and NPU.

The [native texture follow-up](docs/validation/2026-10-08/texture-worker/README.md)
passes GPU-drawn input textures → separate NPU worker → native GPU shader sampling
at 2880×1800 in BGRA and RGBA. It uses no explicit CPU pixel uploads or output
copies. Creator release/reopen and parent/worker termination checks pass, followed
by successful recovery. The private bundle includes both finite texture probes.
This resolves a shared-resource boundary. Windows' general shader/state stream
and actual DWM selection remain unimplemented.

The subsequent [registered-driver laboratory](native/windows/registered_lab/README.md)
now loads through Windows' normal graphics stack in an isolated VM. Restricted
shader draws, image uploads, clears and process sharing pass 62 image checks with
clean removal in the [v31 test](docs/validation/2026-10-09/registered-draw-v31/report.json).
These registered draws explicitly use the CPU provider. The same draw callbacks
separately pass 16 checks on Delta's NPU using test allocation callbacks;
local installed-adapter NPU execution remains unproven. The
[v42 presentation test](docs/validation/2026-10-10/registered-window-v42/report.json)
now verifies 12 displayed frames through the registered adapter, including three
patterned CPU shader blends. It checks the actual window's screen pixels and
unloads cleanly. The original desktop and DWM remain active, and no Tahoma
speedup is established. The subsequent
[v43 NPU test](docs/validation/2026-10-10/registered-npu-window-v43/README.md)
executes three registered-driver draws on Delta’s physical NPU through an explicit
remote laboratory transport and verifies all 552,960 screen RGB bytes within one
byte. Local CPU/GPU draw-provider counts stay zero; CPU transfers, kernel copies
and the original desktop compositor remain. The
[indirect-display diagnostic](native/windows/indirect_lab/README.md) is the next
adapter-selection test, not an implemented desktop mode.

The isolated desktop work now captures DWM's real shader and draw state. An
[offline NPU replay](docs/validation/2026-10-10/dwm-draw-replay-npu-v53/report.json)
executes its first indexed draw through vertex shading, projection, triangle
coverage, pixel shading and BGRA output, matching all 786,432 output pixels.
This draw is an opaque uniform-color quad. The
[indexed driver callback](docs/validation/2026-10-10/indexed-ddi-npu-v60/report.json)
now passes two NPU draws through test allocation callbacks, including constant
updates and checked index offsets. An
[installed compositor test](docs/validation/2026-10-10/indexed-dwm-v61b/report.json)
then executed three real DWM draws on the physical NPU, with zero received or
displayed frames. General textured desktop rendering and indirect-display frame
delivery remain unfinished. The
first cold replay took 9.76 seconds including compilation. A subsequent
[rectangle specialization](docs/validation/2026-10-10/solid-rectangle-npu-v58/report.json)
preserves the exact image and reduces repeated draw time to 42–49 ms in the
diagnostic run. Its NPU pixel tests check 37.9 million components, including
fractional edges and unchanged background pixels. This still does not establish
interactive desktop performance or a Tahoma benefit. A
[native packet bridge](docs/validation/2026-10-10/indexed-stream-v65/report.json)
now returns exact 1024×768 NPU output from the physical machine to the disposable
VM. Warm roundtrips took 203 ms in the diagnostic run. The bridge uses a local
named pipe and a bounded authenticated stream; it is laboratory infrastructure,
and its successful packet tests do not establish desktop frame delivery. The
[next textured draw](docs/validation/2026-10-10/point-texture-npu-v71/report.json)
now has an exact NPU specialization for opaque, one-to-one point-sampled quads.
Its captured 1024×768 replay passes, as do changing-image and driver callback
tests. This is a checked subset, not general desktop rendering.

The [latest installed trial](docs/validation/2026-10-10/sole-display-v95/report.json)
completes 269 live desktop NPU draws plus 13 prewarm requests. Four frames arrive
through the sole indirect display, including wallpaper, icons and taskbar
controls. A complete taskbar draw then exposes an NPU compiler layout failure.
Offline replay reproduces the failure at 4096 and 1024 pixels per batch; 512 and
256 pixels pass with exact coverage and at most one color-byte error against the
scalar reference. The input test incorrectly required an opaque taskbar and
never sent Start or Escape. Both custom drivers unload cleanly and the normal VM
desktop returns. Stable interactive desktop offload and a Tahoma benefit remain
unproven; pixel clears also still use the CPU.

[Mirrored-texture qualification](docs/validation/2026-10-10/mirror-v174/report.json)
now replays that failed DWM blur draw on the NPU with all 8,192 output pixels
matching the CPU and scalar reference. MIRROR addressing crosses the driver,
versioned packet and NPU sampling paths. All 22 CPU suites, 196,608 native NPU
sample components, 58 driver draws and the existing NPU regressions pass.
The installed desktop trial is the next validation step.

The [loop-shader qualification](docs/validation/2026-10-10/loops-v165/report.json)
passes the captured 1,850-word mirrored filter on the NPU, including 33 scalar
comparisons, a scissored raster draw and native draw-packet delivery. General
admitted loops use a checked 16-iteration bound. The captured filter has an exact
bytecode specialization with up to 128 sample pairs and a verified 256-pixel
batch limit. A larger batch lost the NPU device during qualification; the failed
experiment and subsequent recovery check are retained. Twenty CPU suites and
57 NPU indexed DDI draws pass. The v93 installed trial reaches this
1,850-word shader, then rejects its MIRROR sampler state before NPU execution.

The [masking-shader qualification](docs/validation/2026-10-10/masks-v167/report.json)
adds bitwise AND and floating inequality with conservative predicate tracking.
The captured 223-word shader matches the scalar reference exactly on the NPU for
point and linear sampling. Its scissored raster and native draw packet also pass,
alongside 21 CPU suites and the NPU regression checks. The v92 installed trial
accepts its creation but still fails to deliver a complete desktop. A
[longer finite observation helper](docs/validation/2026-10-10/desktop-duration-v29/report.json)
now builds and passes ownership/readback contracts for the next installed test.

[NPU-computed triangle bounds](docs/validation/2026-10-10/triangle-bounds-v112/report.json)
now restrict tile dispatch while preserving exact output in eight replays of four
captured shell operations. Three warm draws drop from 8.27/1.75/4.43 seconds to
0.82/0.59/0.58 seconds. These are individual draw measurements; usable desktop
frame delivery remains the next validation boundary.

The [next Start menu capture](docs/validation/2026-10-10/sparse-budget-v116/report.json)
now passes on the NPU with exact CPU/reference pixels. Its 35 instances need 70
raster tiles; the old estimate rejected it as 7,560 tiles. Admission now uses
device-computed bounds and retains the 4,096-tile maximum. The v82 desktop
attempt failed to start tracing before installing any drivers; the disposable
VM was restarted and the [failure preserved](docs/validation/2026-10-10/sole-display-v82-preinstall/report.json).

An explicit [NPU model cache](docs/validation/2026-10-10/model-cache-v118/report.json)
now loads all six models in a captured draw from cache in a fresh process,
reducing compilation from 4.47 seconds to 0.16 seconds with exact pixels.
Rendering itself still takes seconds. The corrected collector also passes 100
snapshots during concurrent writes; both changes await desktop validation.

The [captured-window desktop experiment](native/windows/capture/README.md) now
presents an NPU-composed 2880×1800 desktop with input passing through to the
original applications, a disable/enable control, and an external hang watchdog.
It retains GPU capture/client rendering and fails equivalence to Windows' original
shadows/taskbar effects, so it is **not full desktop offload**. A separate
[shared allocation experiment](native/windows/wddm-memory.md) establishes real
cross-process NPU buffers and GPU consumption without a CPU upload of the result.
Neither is a system graphics adapter or evidence of a Tahoma improvement.

Texture sampling, affine transforms and specialized bilinear scaling now share the
native switch. The optimized NPU resize/compose API measured about 12 ms at 720p
and 27 ms at 1080p, including host memory work. These are off-screen operation
measurements; see [resize evidence](docs/validation/2026-10-06/resize/README.md).

A native Windows window now shares byte buffers between a D3D12 compute shader and
the NPU, follows the saved selection, and preserves its active backend on invalid
choices. With NPU driver 5540 active, GPU/NPU pixel checks pass at 720p and 1080p.
A 720p switching run and all twenty native inference trials completed on an active
desktop with every presentation returning `S_OK`; scanout timing remains unmeasured.
The private CI Windows bundle runs with `run.cmd`; press **1 / 2 / 3** to switch
GPU / NPU / CPU. See [native usage and limits](native/interop.md#native-windows-application).

The grouped-pixel NPU graph halves shared-buffer call time: about 5 ms at 720p and
9.5 ms at 1080p. A 1080p control run measured 13.9 ms median complete NPU processing
while occluded; the subsequent active-desktop evidence is at 720p. Exact 2× downscale/compose now
takes about 5 ms through the host API. See the
[layout and downscale measurements](docs/validation/2026-10-06/layout/README.md).

## Run it

Python 3.11–3.14 and a compatible installed device driver are required. The runtime
is pinned to OpenVINO 2026.1.0 to reproduce the first Panther Lake experiments.

```sh
uv sync --frozen
uv run npu-render devices
uv run npu-render stack
uv run npu-render select npu
uv run npu-render demo --output results/npu-frame.ppm
uv run npu-render select gpu
uv run npu-render demo --output results/gpu-frame.ppm
```

On a machine without an Intel NPU/GPU, use `select cpu`. A missing or unsupported
device returns an error. Selecting a backend compiles and checks a 16×16 fixture
before saving the selection; every new renderer separately qualifies its actual
shape. This selection is not a guarantee that arbitrary shapes will work.

For driver comparisons, `stack --expected-npu-driver 1005540` requires the exact
version reported by the loaded runtime. An installed driver package can differ
from the driver still executing before a required restart. Missing or mismatched
versions return failure with the captured report. The compiler-type property in
this inspection is a runtime preference; a compiled graph can resolve it further.

On Windows, `npu-render session --require-desktop` checks the current process's
session and desktop without changing either. The native contention benchmark
requires a connected `WinSta0\Default` desktop before inference warmup and around
each trial. This rejects session zero, a screen saver, and inaccessible desktops;
passing the check does not establish visible frames or physical scanout.

Selection precedence: explicit argument → `NPU_RENDER_BACKEND` → saved config →
GPU. `NPU_RENDER_CONFIG` or CLI `--config` selects a different config file.
`npu-render status` shows the effective selection. Existing applications call
`reload_selection()` on their control thread to apply saved changes.

```python
from npu_render import Composition, Renderer

renderer = Renderer(Composition(width=1920, height=1080, layers=1))
# Inputs: contiguous, writable NumPy FP16 arrays of shape (1, 4, 1080, 1920).
# Colors are premultiplied linear-sRGB RGBA; caller owns buffer synchronization.
frame = renderer.compose(background, [foreground])

renderer.switch("NPU")  # Prepare/qualify, then activate between frames.
frame = renderer.compose(background, [foreground])
print(frame.backend, frame.execution_devices)
```

Switching can take seconds and belongs on a control thread. Frames can continue
using the old backend during preparation. A failed switch leaves it active and
raises an error. Each returned frame owns its pixels. Inputs must remain unchanged
for the duration of a synchronous call. A graphics integration must handle format
conversion, fences, deadlines, damage, presentation, and device-loss policy.

## Evidence and direction

### Live switching and concurrent inference

```sh
uv sync --frozen --extra live
uv run npu-render live --backend GPU --seconds 60 --sequence NPU,CPU,GPU --switch-every 15
```

The window animates deterministic translucent surfaces. Press **1** for GPU,
**2** for NPU, **3** for CPU, **R** to reload the saved selection, or **Escape** to
exit. Compilation runs on a control thread while the previous backend keeps
rendering. Reports include actual execution devices, switch failures, each frame's
scene/composition/conversion/submission times, and skipped replay slots. The
software window still passes through the operating system compositor. Submission
timing is not physical scanout timing.

An optional C extension speeds up CPU color conversion; installation falls back to
NumPy if a compiler is unavailable. The report identifies the implementation.
This conversion is explicitly CPU work, not attributed to the NPU.

Against an existing Tahoma server running its model on the iGPU:

```sh
uv run npu-render-benchmark --endpoint http://127.0.0.1:8000/v1 \
  --model YOUR_MODEL --inference-device GPU --rounds 5 --requests 3 \
  --report results/contention.json
```

This rotates GPU/NPU/CPU/idle graphics conditions, keeps the prompt and animation
fixed, and saves raw per-frame and per-request results after every trial. Token
throughput uses server-reported usage; missing usage remains unknown. Text chunk
arrival times are recorded separately from engine token timings. The declared
inference device must be verified against server logs/telemetry. See the
[measurement protocol](docs/live-benchmark.md) before interpreting differences.

### Original device experiments

The original Panther Lake study measured median NPU graph calls of 4.34 ms for a
1080p one-foreground-layer blend and 5.55 ms for a 720p four-layer blend. The
four-layer 1080p case took 12.87 ms with tail samples exceeding a 60 Hz interval.
These are synthetic calls without presentation; CPU/GPU were faster on the same
four-layer graph. They establish a credible implementation path, not a Tahoma gain.

- [First-principles study and measured limitations](docs/research/first-principles.md)
- [Original probe, preserved for reproducibility](research/probe.py)
- [Original hardware evidence](docs/research/npu_graphics_delta_evidence.json)
- [Packaged runtime hardware validation](docs/validation/README.md)
- [Live rendering and Tahoma results](docs/validation/2026-10-06/README.md)
- [Driver activation and native Tahoma comparison](docs/validation/2026-10-07/README.md)
- [Native compositor adapter](native/README.md)
- [D3D12/NPU buffer interoperability](native/interop.md)
- [Operation coverage ledger](docs/coverage.md)
- [Architecture and operation boundary](docs/architecture.md)
- [Milestones toward the system-wide switch](docs/roadmap.md)

```sh
uv run python research/probe.py --device NPU --precision f16 --report results/probe.json
uv run python tools/validate_switch.py --backends GPU NPU CPU --report results/switch.json
```

## Development

**Every commit must follow Conventional Commits.** Merge, revert, and release
messages have no exemptions. Local hooks and CI enforce the policy. See
[CONTRIBUTING.md](CONTRIBUTING.md) and [AGENTS.md](AGENTS.md).
GitHub server-side rules for this private repository are currently unavailable
under the account's plan; see [the enforcement status](docs/governance.md).

```sh
npm ci
npm run hooks:install
uv run pytest
uv run ruff check .
uv run ruff format --check .
```

Keep this repository private. There is no package-publication workflow.
