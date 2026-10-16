# Exact integer blending for unit-modulated point rectangles

The new kernel preserves all 233 existing ABI fixtures and all 16,777,216
source/background/alpha byte combinations on the NPU. Both the old and new device
kernels match an independent FP32 oracle for every exhaustive input. Original
carrier checks pass before and after. No tolerance, CPU rendering fallback,
model-metadata change, or changed interpolation rule is introduced.

The change is motivated by profiling three retained draws. In the large
2880-by-1800 rectangle, the three warm variants spend 1,023.562 ms waiting for
color requests within 1,635.662 ms of total merger work. Coordinate requests and
host movement account for the remainder. All four variants have unit modulation.
The profiling adapter changes timing instrumentation only; all twelve tested
images retain their original NPU and CPU-reference outputs. These request waits
include driver/runtime overhead and are not hardware execution counters.

For unit modulation, each source-over channel is exactly
`round(clamp(source + background * (255 - alpha) / 255))`.
Because 255 is odd, the exact rational result has no half-integer tie. The kernel
uses `min(source*255 + background*(255-alpha), 65025)`, adds 127, and performs an
exact reciprocal division by 255 over the bounded integer range. Independent
FP32 checks, compiled old/new CPU kernels, and both NPU kernels agree exhaustively,
including non-premultiplied byte combinations. Format conversion and opaque-alpha
replacement remain explicit. Other modulation colors retain the existing FP32
path. The kernel occupies 4,656 of the qualified 18,256 code bytes.

All 148 complete-image variants for the 37 admitted rectangle captures match the
original NPU renderer and the original rectangle graph executed on CPU. Their
111 warm pairs sum to 12,771.900 ms on the original renderer and 4,164.084 ms on
the new candidate, with ten slower pairs.

A separate alternating-order comparison directly compares the immediately
preceding rectangle implementation with the new kernel. All 148 images remain
identical. Across 111 warm pairs, summed draw time falls from 5,863.759 to
4,516.852 ms, a 22.97% component reduction, with 29 individual pairs slower.
This comparison isolates the kernel
change; it does not establish desktop responsiveness, GPU capacity, or Tahoma
performance. Both adapters use the same rectangle scheduling policy and retain
the same qualified NPU crop path.

The first paired diagnostic fails its build because MSVC rejects deprecated
`getenv` under warnings-as-errors. The next revision uses a bounded Windows
environment-variable read; rendering code is unchanged. The original failed
build and stopped coordinator are retained. Earlier profiling also stopped at
the preceding desktop analysis failure before executing; its retry requires
the actual successful live/cleanup proof. These failures are not overwritten.

Model SHA-256:
`d651d6101dbccc5796873dc2016ec9e23d262cd955e9bc4bee6d56b86756eb2d`.
The archive pins 274 source and evidence artifacts. Private pixels, model blobs,
compiled binaries and caches are excluded. Worker replay and live desktop
integration remain subsequent gates.
