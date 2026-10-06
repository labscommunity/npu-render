# npu-render

A switchable graphics runtime that moves supported rendering operations between
NPU, GPU, and CPU execution. The objective is a single control that frees GPU
resources for workloads such as Tahoma, with application and compositor adapters
providing the integration boundary.

**Status: private prototype.** The runtime implements deterministic premultiplied
RGBA composition and an explicit backend switch. The packaged runtime passed
GPU → NPU → CPU → GPU → NPU switching on Panther Lake. It supports CPU/GPU/NPU through
its first OpenVINO provider; other providers can implement the same contracts.

The switch currently affects applications using this library. System-wide desktop
composition, arbitrary Direct3D/Vulkan games, display output, and codec replacement
are integration milestones, not implemented features. Tahoma acceleration remains
a measurement goal.

## Run it

Python 3.11–3.14 and a compatible installed device driver are required. The runtime
is pinned to OpenVINO 2026.1.0 to reproduce the first Panther Lake experiments.

```sh
uv sync --frozen
uv run npu-render devices
uv run npu-render select npu
uv run npu-render demo --output results/npu-frame.ppm
uv run npu-render select gpu
uv run npu-render demo --output results/gpu-frame.ppm
```

On a machine without an Intel NPU/GPU, use `select cpu`. A missing or unsupported
device returns an error. Selecting a backend compiles and checks a 16×16 fixture
before saving the selection; every new renderer separately qualifies its actual
shape. This selection is not a guarantee that arbitrary shapes will work.

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

The original Panther Lake study measured median NPU graph calls of 4.34 ms for a
1080p one-foreground-layer blend and 5.55 ms for a 720p four-layer blend. The
four-layer 1080p case took 12.87 ms with tail samples exceeding a 60 Hz interval.
These are synthetic calls without presentation; CPU/GPU were faster on the same
four-layer graph. They establish a credible implementation path, not a Tahoma gain.

- [First-principles study and measured limitations](docs/research/first-principles.md)
- [Original probe, preserved for reproducibility](research/probe.py)
- [Original hardware evidence](docs/research/npu_graphics_delta_evidence.json)
- [Packaged runtime hardware validation](docs/validation/README.md)
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
