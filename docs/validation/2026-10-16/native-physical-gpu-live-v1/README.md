# Physical GPU baseline through the desktop bridge

The finite GPU control passes 550 seconds of presentation, 519 exact physical
submissions, forty controlled inputs and restoration. The IDD uses the verified
Intel GPU `8086:B080`; there are zero NPU receipts, child receipts or provider
generations. The primary topology, ordinary windows and pointer state restore.
Memory integrity remains enabled.

| Measurement | GPU control | NPU worker v19 |
| --- | --- | --- |
| Animation updates within ten scheduled seconds | 299 | 7 |
| Verified updates per second | 29.9 | 0.7 |
| Unique animation updates including drain | 300 | 9 |
| Warm mouse sent-input → display median | 59.473 ms | 94.649 ms |
| Warm mouse sent-input → display p95 | 69.442 ms | 107.663 ms |
| Warm keyboard sent-input → display median | 56.849 ms | 89.572 ms |
| Warm keyboard sent-input → display p95 | 73.235 ms | 1,632.388 ms |

Both trials use the same 1024×768 logical desktop, physical presenter, independent
observer, forty-event input executable, and ten-second 512×256 color animation
scheduled at 30 Hz. Both producers emit all 300 animation updates without skips.
All 300 GPU updates reach matching display counters, with the last appearing
after the scheduled interval. The same bridge can therefore carry this workload
at approximately the requested rate. Together with the NPU publication trace,
this localizes the large animation deficit to the NPU rendering path.

These are separate finite runs, not an alternating-order performance experiment.
The GPU control omits the NPU trial's earlier eight-operation admitted application;
background Windows activity and thermal state are not identical. Nineteen warm
inputs per kind make nearest-rank p95 equal to maximum. Display counters do not
measure panel photons. Physical pixel-read timing includes observer cadence.
Neither run measures Tahoma throughput or proves reduced GPU resource use.

The [manifest](artifact-sha256.json) retains 98 artifacts, including all controllers,
route evidence, input/display logs, recovery evidence and analysis. Remote files
match SHA-256 receipts. The outer coordinator contains a fallback recovery call
with the wrong parameter name (`-Transaction` instead of `-Root`); that branch
was never executed. Normal restoration and the independently armed recovery path
use the correct interface. Future coordinators must use `-Root`; this immutable
record retains the script actually executed.
