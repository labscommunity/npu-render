# Four-pixel textured batching on the NPU

The vector kernel passes all 144 exact NPU fixtures, 42 complete comparison
images and four full-texture interruption/retry scenarios. At 1024×768, its
matched batched-render median is **14.5765 ms**, versus **30.8553 ms** for the
scalar kernel: **52.76% lower**.

Four adjacent destination pixels are processed together. Texture sampling,
format conversion, channel masks, ordered source-over arithmetic and
per-command rounding are unchanged. Scalar prefixes and tails handle unaligned
span boundaries. The kernel occupies 4,128 of the existing 18,256 code bytes;
compiler metadata and scheduling remain untouched.
Model SHA-256:
`1195240d499e23aeb203047529e5da655d9c62528b2307d5e55922da8fb7650e`.

## Same-work comparison

Both models render the same four already-batched commands. They have identical
packet sizes and submission counts, and request the same verified turbo hint.
Three A/B/B/A rounds per resolution follow one warmup per model.

| Image | Scalar median | Vector median | Reduction | Requests for either model |
| --- | ---: | ---: | ---: | ---: |
| 128×128 | 3.8637 ms | 1.9621 ms | 49.22% | 1 |
| 1024×768 | 30.8553 ms | 14.5765 ms | 52.76% | 4 |
| 1920×1080 | 82.7661 ms | 39.7020 ms | 52.03% | 12 |

Each median has six observations in one process. The interval includes packet
preparation, NPU waits and output assembly. Initial texture/reference preparation
is excluded. All 42 outputs, including warmups, match the independent integer
oracle. Eight original-model checks before and after custom qualification pass.

## Owned windows and recovery

The unchanged window planner also passes four NPU scenarios with this kernel.
Each owns eight texture snapshots and 32 ordered commands, rejects malformed
trailing commands before submission, ignores later caller mutations, discards
partial output after an injected exception and retries after model recreation.
The two 48 MiB source cases take **43.8907 ms** and **47.5509 ms**, with eight
and nine successful retry submissions. These are single observations after model
recreation; snapshot creation, window planning and model import are excluded.
They are not a matched comparison against the earlier window timings.

No process kill, real device loss, asynchronous stale completion, Windows queue
integration, actual desktop display, GPU-relief measurement or Tahoma trial is
performed here. General shaders, filtering and arbitrary transforms are outside
this packet contract. These results leave the physical desktop baseline at
0.9 verified animation updates/s.

`artifact-sha256.json` identifies 63 artifacts and 158,124 remote source bytes.
CPU and NPU complete-image hashes agree; model binaries and pixel payloads are
excluded from the repository.
