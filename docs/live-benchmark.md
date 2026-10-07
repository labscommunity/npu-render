# Live switching and Tahoma measurement

The live harness is an application integration. It owns an animated scene and
offloads source-over composition through the shared runtime. SDL submits the
resulting opaque software window to the operating system. Neither Windows DWM nor
the display engine moves to the NPU in this integration.

## Reproduction

Install `uv sync --frozen --extra live`, then run `npu-render live --backend GPU`.
The optional CPU conversion extension builds with a C compiler (MSVC Build Tools
on Windows); the NumPy path remains available if the extension cannot build.
Reports name `native_cpu` or `numpy_cpu`. Both implement the same transfer table.
The exhaustive FP16 conversion test and buffer-validation tests check this path.

The default scene is 1280×720 with two moving translucent rectangles over an opaque
gradient. It is SDR premultiplied linear-sRGB, FP16, NCHW. Scene motion follows the
scheduled replay time, so a late renderer skips old scene positions rather than
accumulating a queue. This produces identical desired traces, but delivered traces
can differ when a backend is overloaded; report that difference.

## What timing establishes

- `scene_ms`: CPU update of input surfaces.
- `compose_ms`: synchronous render call, including owned output and associated copies.
- `convert_ms`: CPU linear-sRGB-to-RGB8 conversion and planar-to-packed conversion.
- `submit_ms`: software surface update and display submission API return.
- `work_ms`: sum of those four costs.
- `processing_deadline_misses`: submissions completed after the scheduled frame budget.
- `skipped_replay_slots`: target animation slots omitted to avoid queue growth.

None of these count physically displayed frames or scanout misses. SDL's window
system, DWM or an outer Wayland compositor, may buffer or compose after submit.
`--headless` explicitly removes the window; an SDL dummy/offscreen driver is also
marked as offscreen, never accepted as real display validation. The tests may use
dummy SDL, while hardware reports must state the actual driver/session.

## Inference trial protocol

Run Tahoma with a fixed real model and explicit GPU device, on an otherwise idle
machine with a fixed power mode. Record binary/model/runtime versions and backend
evidence. `--inference-device GPU` is a declaration, not independent verification.
The harness does not start or stop the server or download models. Optional API
authentication comes from `NPU_RENDER_API_KEY`, which is never written to reports.

The benchmark warms inference once and runs at least five rounds. Each round
rotates the conditions GPU, NPU, CPU, IDLE; after a full rotation it reverses their
order. Every trial prepares/qualifies its renderer before measurement, settles
for two seconds, then renders while a worker thread makes identical sequential
requests. IDLE displays one static CPU-composed frame before measurement and does
no repeated rendering. Compiled models are not deliberately cached across modes.

Each trial saves raw frame records and inference records. A failed request,
truncated SSE stream, or closed graphics window fails the benchmark and preserves
already completed trials. A report with `status: failed` is not a completed run.

Reported inference measurements:

- Time to first **content chunk**, measured at the client. A backend that returns
  its whole completion in one chunk does not expose time to first generated token.
- End-to-end tokens/s using server `usage.completion_tokens` divided by elapsed
  request time. Missing usage yields `null`, never a token estimate from chunks.
- Content-chunk arrival timestamps and declared per-chunk token counts where the
  server supplies them. Gaps between chunks are not presented as per-token latency.
- A SHA256 of returned content, so deterministic-output changes can be detected
  without storing response text.

Interpret the paired NPU/GPU throughput ratios together with frame budgets, output
hashes, token counts, CPU/idle controls, and system telemetry. Lower graphics load
because frames were dropped can inflate inference throughput. A repeatable 10%
gain is an initial product target, not a claim made by a passing harness test.
Follow a promising short comparison with a sustained thermal/power run and then
native compositor measurements.

## Measure Windows' existing compositor

`nr_desktop_motion` creates three static GDI windows and either keeps them still
or moves them through a repeatable trace. Windows DWM performs the composition.
It changes only its own windows, does not capture screen content, and exits after
the requested duration. Closing any workload window stops the trace. It requires
an unlocked interactive desktop. The report separates repaint/movement counts
from display delivery; this program does not offload anything to the NPU.

Build with `tools/build_native.py`, then, on Windows with a running Tahoma endpoint:

```powershell
uv run python tools/benchmark_desktop.py --executable build/native/Release/nr_desktop_motion.exe --endpoint http://127.0.0.1:18080/v1 --model MODEL_ID --inference-device GPU --report results/desktop-benchmark.json
```

The harness rotates five pairs of static/moving conditions, each with three
requests. It requires desktop checks before/after each trial and preserves token
counts, output hashes, timing, and the workload report. Motion changes CPU/window
management as well as GPU composition; a measured difference is not a prediction
of an NPU compositor's performance.

Capture GPU activity concurrently from a shell with access to the counters:

```powershell
build/native/Release/nr_dxcore_probe.exe --d3d-ml > results/adapters.json
./tools/windows_gpu_telemetry.ps1 -OutputPath results/engines.jsonl -Seconds 60
uv run python tools/analyze_gpu_telemetry.py results/engines.jsonl --adapters results/adapters.json --output results/engines-summary.json
```

The DXCore probe returns 1 when it finds no NPU, including when `--d3d-ml` produces
a valid GPU adapter list. Match adapters and capture from the same boot/device
configuration. `Seconds` retains the old parameter name but denotes a sample
count. Each independently primed query refreshes wildcard instances so newly
launched processes appear. This produces gaps between measured intervals;
elapsed duration is longer than the nominal sample count. It is sampled discovery,
not an uninterrupted trace. Short-lived processes can be missed.

The analyzer rejects invalid counter statuses/values, separates process lifetimes,
and retains distinct adapters/engines, including unnamed engines. Its ranking is
the busiest *mean observed hardware engine* per process, not a sum of engines or
an instantaneous Task Manager percentage. Unknown process identities are excluded.
An engine named `Neural` is not proof of NPU execution: inspect its adapter LUID.

Use a positive GPU control before interpreting zeros. For presentation details,
collect a separate bounded [PresentMon trace](https://github.com/GameTechDev/PresentMon)
with a unique session name, retaining dropped frames and warnings. Its event-based
display/GPU metrics do not replace physical scanout or power measurements.

The [Windows integration assessment](research/windows-compositor.md) describes
the additional boundary required to replace DWM work.

## References

- [SDL-backed Pygame display behavior](https://www.pygame.org/docs/ref/display.html)
- [Surface array transfer](https://www.pygame.org/docs/ref/surfarray.html)
- Tahoma's `cascadia-api` SSE implementation: usage frames can count model tokens
  separately from text chunks, including speculative batches and final-only output.
