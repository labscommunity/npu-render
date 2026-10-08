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
conformance. The private Windows bundle includes this independently testable step.

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
