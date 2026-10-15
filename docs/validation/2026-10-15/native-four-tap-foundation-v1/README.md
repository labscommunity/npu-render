# Captured geometry and NPU profiling (2026-10-15)

The retained slow desktop request is a four-sample image filter over a rectangular
quad. The capture uses a 2880×1800 BGRA texture, a 1024×768 target and scissor
`[0,64,1024,704]`. Its six projected vertices form that rectangle, with uniform
white modulation, corner UVs from zero to one and source-over blending. The upper
screen coordinate before snapping is `64.0000076`, so a replacement must preserve
interpolation and helper-lane derivative behavior, not assume exact integer
geometry. Inspection here uses the CPU reference and performs no rendering.

Packet SHA-256:
`75d41f0b2edb0ed4a2edd095303d7655dd7c428c12f3dd5098281e81d3acbfeb`.
The inspector links unchanged qualified `native-routing-v45` libraries. Its
source, clean build, binary hash and geometry output are retained. Private packet
pixels remain outside the repository.

A separate paired scalar/vector rectangle experiment establishes a measurement
limitation: all 64 profiling reads from each reused inference request returned
exactly the same task time, even as inputs and host timings changed. Those values
are stale for per-iteration analysis and must not be used to apportion latency.
The pinned OpenVINO source delegates these queries to its profiling interface;
this inspection does not establish the lower-level cause.

Repeating with a fresh request for each inference produces varying task times and
passes exact output checks. After four initial iterations, 28 paired cases give:

| Variant | Median request and output time | Median reported NPU task time |
| --- | ---: | ---: |
| Scalar | 7.79505 ms | 6.2105 ms |
| Vector | 6.48685 ms | 4.8295 ms |

The order alternates. Both variants receive the same 4 MiB payload and render
4,096 pixels. Request construction is outside the timer, but first-use effects
can remain. Original Gather health checks pass before and after. These timings
establish a bounded component improvement; they do not measure the captured
four-sample shader, desktop responsiveness or Tahoma benefit. The NPU reports
three available compute tiles; these carriers use one.

No desktop, driver or Tahoma state changed. The live renderer remains unchanged.
The next step is an exact comparison of a fused four-sample implementation on the
retained desktop request, including complete preparation and output costs.

`artifact-sha256.json` authenticates 35 retained evidence/source files. Remote
artifacts were hashed before download and verified locally. Profiling models and
compiled kernels are excluded. Source provenance points to the pinned OpenVINO
revision `e213a147257`; the model-profiling wrapper alone does not explain the
observed stale query data.

Archived Python sources use `.py.txt` to preserve their original bytes without
treating measurement scripts as maintained runtime modules. Remove the final
`.txt` suffix when reconstructing a retained experiment.
