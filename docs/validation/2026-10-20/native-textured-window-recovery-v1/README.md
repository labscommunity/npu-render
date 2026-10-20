# Full texture snapshots through bounded NPU windows

Four complete-image scenarios pass on the NPU using the qualified textured-batch
kernel. Each owns eight full texture snapshots and 32 ordered commands. The
larger cases retain **48 MiB** of source textures while every packet stays
within the 196,544-byte texture budget.

The host copies immutable texture windows and assembles completed tiles.
Coverage, point sampling, format conversion and quantized blending execute on
NPU. When a tile's textures exceed the budget, its ordered commands are split
across packets and consume the preceding packet's destination.

| Case | Target | Source snapshots | Retry requests | Maximum texture bytes per packet | Retry render time |
| --- | --- | ---: | ---: | ---: | ---: |
| 0 | 257×259 RGBA | 12,673,120 B | 2 | 185,568 B | 14.5893 ms |
| 1 | 257×259 BGRA | 12,673,120 B | 3 | 195,752 B | 18.0437 ms |
| 2 | 1024×768 RGBA | 50,331,648 B | 8 | 196,080 B | 92.3188 ms |
| 3 | 1024×768 BGRA | 50,331,648 B | 9 | 196,460 B | 108.7820 ms |

For each case, an exception is injected after the first NPU submission has
produced partial output. The original checkpoint remains unchanged. Recreating
the model and retrying the whole operation returns the exact reference image.
Caller mutations after snapshot capture do not affect rendering. A malformed
trailing command is rejected during planning before a provider submission.

There are eight model generations and 26 total submissions, including four
interrupted submissions. This is model recreation after a simulated exception,
not process/device loss or a test of asynchronous stale completions. The timed
retry includes packet packing and NPU waits but excludes snapshot creation,
window planning and model import. Each time is a single diagnostic observation.

`artifact-sha256.json` identifies 20 artifacts and 33,609 remote source bytes.
CPU and target-device results have matching complete-image hashes.

Windows allocation identity, external aliases, the existing C++ command ledger,
map/flush/present ordering and general shaders are not integrated. These synthetic
windows do not qualify the captured desktop's texture mappings, a usable frame
rate, GPU relief or Tahoma benefit.
