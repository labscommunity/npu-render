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

## References

- [SDL-backed Pygame display behavior](https://www.pygame.org/docs/ref/display.html)
- [Surface array transfer](https://www.pygame.org/docs/ref/surfarray.html)
- Tahoma's `cascadia-api` SSE implementation: usage frames can count model tokens
  separately from text chunks, including speculative batches and final-only output.
