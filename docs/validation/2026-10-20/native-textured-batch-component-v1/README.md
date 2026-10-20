# Ordered textured batching on the NPU

The new component passes **144 exact target-device fixtures**, with eight
original-model checks before and after custom execution. It handles ordered
solid and one-to-one point-texture rectangles, RGBA8/BGRA8 conversion,
copy/source-over blending and channel masks. Unsupported packet states are
rejected before publishing any destination pixels.

The kernel is 2,992 bytes inside the existing 18,256-byte code allocation.
Compiler metadata, allocations, DMA operations and the six-engine schedule are
unchanged. The twelve input/output spans execute as sequential pairs per engine.
Model SHA-256:
`a0ef05ffe0336c6c6bbf299a9c91a101e48253f36f2bb92f43d384d15fe60af6`.

## Matched comparison

Three A/B/B/A rounds compare separate and batched execution of the same four
ordered draws using the same model. Every complete output matches the independent
integer reference: 42 images including warmups.

| Image | Separate median | Batched median | Reduction | Requests |
| --- | ---: | ---: | ---: | --- |
| 128×128 | 7.0911 ms | 3.9878 ms | 43.76% | 4 → 1 |
| 1024×768 | 47.4941 ms | 31.3517 ms | 33.99% | 16 → 4 |
| 1920×1080 | 132.0890 ms | 84.5157 ms | 36.02% | 48 → 12 |

Each median has six timed observations in one process. Both paths explicitly
request and verify the same NPU turbo setting. Loop time includes packet
preparation, NPU waits and output assembly; initial texture/reference preparation
is excluded. Fixed packet traffic falls with submission count. These are
synthetic texture windows, not a replay of Windows desktop frames.

The 31.35 ms case excludes geometry, general shader evaluation, Windows resource
handling and presentation. It does not establish a 30 fps desktop. The current
physical desktop baseline remains 0.9 verified animation updates/s.

## Evidence and bounds

Each span admits at most 16,384 destination pixels, 32 commands and 196,544 bytes
of immutable texture storage. All commands are validated before drawing.
The first CPU fixture mistakenly allocated 196,608 texture bytes and was
correctly rejected; its failure and original fixture source are retained.
The corrected fixtures use three 127×128 textures and pass on CPU and NPU.

`artifact-sha256.json` identifies 49 artifacts, with 126,593 remote source bytes.
No model binary or pixel payload is committed. This does not implement a Windows
queue, arbitrary sampling/transforms, resource alias handling, GPU relief or
Tahoma acceleration.
