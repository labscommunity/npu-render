# Compact filled-batch image comparison

All 463 recorded generic-program draws pass four image variants each: original,
zero alpha, half alpha and seeded random pixels. All 1,852 candidate images are
byte-for-byte identical to the preceding NPU implementation. The candidate uses
the compact ABI-three kernel and worker v18's filled-batch submission policy.

The 1,389 warm paired measurements total 76,773.4447 ms for the preceding renderer
and 59,582.3367 ms for the candidate, about 22.4% less aggregate time. There are
121 slower candidate pairs. This measures complete component draws in alternating
order; it is not a desktop latency, GPU utilization or Tahoma benchmark.

The independent FP32 CPU reference disagrees with both NPU implementations on
93 image variants, by at most one byte per component. Every differing position
and value is identical between the preceding and candidate implementations.
The comparison therefore qualifies exact preservation of prior NPU output; it
does not claim universal agreement with the independent reference or introduce
a new numerical tolerance. The earlier rejected comparison remains archived
separately.

The selected thirteen-draw comparison also passes all 52 images. Its 39 warm
pairs total 4,404.5729 ms versus 3,932.7621 ms, with five slower candidate pairs.
These results admit a separate registered and live desktop trial. Full desktop
offload, sustained responsiveness, GPU relief and a Tahoma benefit remain open.

The manifest pins 981 source and evidence files, including each draw's report.
Private captures, compiled binaries, model blobs and caches are excluded.
