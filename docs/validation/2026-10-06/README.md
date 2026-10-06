# Live rendering and first Tahoma comparison

This is application-level evidence on the same Panther Lake Windows host used in
the original research. It does not establish replacement of Windows DWM, native
display delivery, or arbitrary application/game offload.

## Live switching

A real SDL `windows` software window ran in the active console session. It rendered
1280×720, two translucent foreground layers, at a target 60 Hz and switched
GPU → NPU → CPU → GPU. The initial Python converter consumed about 8.3 ms/frame.
Replacing conversion with the optional CPU C extension reduced that cost to about
1.1 ms. The revised 40-second run submitted all 2,400 scheduled frames with zero
skipped replay slots. One NPU frame missed its processing deadline. NPU median
complete frame work was 9.74 ms; its p99 was 12.22 ms.

These are submission and processing measurements, not scanout timestamps. The
initial pre-commit prototype's exact revision was not captured; its raw report is
preserved as observational evidence. The revised runtime matches the source at
`afd6d49a7afa3c943ab193b09bd836d48ba53a7e`.

## Concurrent inference

The first completed benchmark used the installed Cascadia/Tahoma 0.2.4 executable,
OpenVINO GenAI 2026.3.1.0, and the existing Qwen2.5-7B-Instruct INT4 IR. It explicitly
requested GPU inference; process inspection found the GPU plugin loaded and no NPU
plugin. Renderer execution used the separately pinned OpenVINO 2026.1.0. The machine
used its Balanced power plan. The binary hash is recorded; its exact source revision
was not exposed by `--version`.

Five rounds rotated GPU/NPU/CPU/idle graphics conditions, with three requests per
condition and 128 generated tokens per request. All 60 completions had the same
content hash and token count. Each condition replayed the same 720p/two-layer scene
at 60 Hz. The per-round NPU/GPU median throughput ratios were 1.0071, 1.0602, 0.9572,
1.0157, and 1.0397: a median **+1.6%**, with a range of **−4.3% to +6.0%**.
This does **not** establish a repeatable useful Tahoma improvement or meet the
provisional 10% target.

| Graphics condition | Median end-to-end tokens/s across its 15 requests | Processing deadline misses | Skipped replay slots |
|---|---:|---:|---:|
| GPU | 22.37 | 1,143 / 5,094 submitted | 0 |
| NPU | 23.25 | 1,059 / 5,000 submitted | 3 |
| CPU | 21.75 | 381 / 5,142 submitted | 2 |
| Idle window | 25.65 | No repeated rendering | 0 |

The ratio of pooled medians differs from the paired statistic; the paired statistic
is the primary comparison. Request durations determine trial length, so raw frame
counts differ. Review deadline pressure along with inference metrics. The GPU
condition uses the same OpenVINO blend graph; it is not a tuned native GPU compositor
baseline. The substantial gap to idle motivates reducing host work and memory
traffic before claiming practical offload value.

This inference mode returned an entire completion in one chunk. True time to first
token and per-token latency were unavailable; the reports correctly retain content
arrival timing without relabeling it as token-generation timing. A streaming-server
follow-up is needed. Thermal/power and whole-desktop conclusions remain unproven.

## Streaming follow-up and shared buffers

A second five-round, 60-request run used the same executable/model with continuous
batching enabled, a 1 GB KV cache, one sequence, and prefix caching disabled. This
mode emitted incremental content with declared token counts. Its median paired
NPU/GPU end-to-end throughput change was **+1.0%**, with ratios 1.0034, 1.0962,
1.0060, 1.0105 and 1.0403. This still does not meet the product target. Raw chunk
arrival timestamps are available in `delta-contention-720-cb.json.gz`.

The Windows D3D12 interoperability probe passed eight changing 64×64 FP16 source-over
frames on Arc B390/NPU. D3D12 populated shared input heaps; the NPU imported their
NT handles and wrote to a shared output heap; D3D12 copied that output back for
checking. Maximum pixel error was 0.000488281. Warm NPU calls were approximately
0.28–0.52 ms for this small fixture. This establishes correct shared allocation
handoff with explicit host waits; it does not establish arbitrary texture import,
asynchronous GPU/NPU fence sharing, or a zero-copy desktop.

The pinned Weston integration also passed Linux CI with a real SHM client in
headless and nested compositor modes. CPU execution there is separate from the
Windows NPU validation. Invalid live selections preserved rendering. Native Linux
Panther Lake presentation has not been tested.

## Artifacts

`summary.json` records hashes and aggregate results. The `.json.gz` files contain
the original UTF-8 JSON reports compressed losslessly; SHA256 values refer to the
uncompressed bytes. They include raw per-frame and per-request timing, device
attribution, switch events and errors. No model weights are included.
