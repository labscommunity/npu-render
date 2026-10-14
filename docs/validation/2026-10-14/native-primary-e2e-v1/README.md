# Controlled native primary interaction and recovery

Live v21 passes the complete controlled blue → mouse → green → keyboard → red
sequence through native NPU desktop composition and the physical presenter. Exact
intermediate topology restoration, the captured ordinary window, cursor position,
foreground, and final driver/package/trust cleanup pass. **Full desktop offload
remains unfinished.** This is a finite interaction qualification, not a usable
desktop switch or migration of all application GPU work.

The run uses corrected presenter v16, topology helper v9, the bounded frame reader,
and DXGI observer v3. Their independent qualification is retained in the preceding
[controller investigation](../native-primary-interaction-v1/README.md). NPU worker
v27 and driver package v16 remain unchanged.

| Check | Result |
| --- | --- |
| Native composition | 1,465 NPU receipts and matching acknowledgments; 41 observed native frames. No provider error. |
| Physical presentation | 38 submissions match the verified source sequence, timestamp and producer; every backbuffer pixel check passes. |
| Controlled input | One mouse receipt and one authenticated keyboard/character receipt. The application changes blue → green → red. |
| Output observation | Both sampled locations remain owned by the presenter; blue, green and red are observed. The observer exits successfully after 32 acquired frames and 64 diagnostic pixel-copy commands. |
| Restoration | The 600-second topology trial finishes normally and restores its original topology exactly. Ordinary window, cursor and foreground restoration pass; final physical-baseline cleanup passes. |
| Static cursor | A standard Windows arrow in a completed NPU frame matches a `DrawIconEx` reference exactly: 144 changed pixels, zero RGB error. Cursor motion and confinement remain unqualified. |

Response time remains far from usable. In this instrumented run, the mouse event
takes 20.08 seconds to reach the application and green is observed 56.82 seconds
after injection. The keyboard reaches the application in 66.74 ms, while red is
observed 56.33 seconds after injection. These are individual observations with
measurement overhead, not a matched benchmark or a p95 estimate. The fixture
repaints every 15 seconds, so frame counts do not measure maximum throughput.
See [report.json](report.json) for the event timestamps and clock frequency.

The physical presenter reports composed presentation during the screen observation
and overlay presentation later, after the observer exits. The documented enum
values are `0 = COMPOSED`, `1 = OVERLAY`, and `2 = NONE`. This temporal sequence
does not isolate the observer's effect, quantify residual GPU work, or establish
zero GPU rendering cost. A matched measurement with diagnostic readbacks removed
is still required.

The first cursor comparison fails: its current cursor has hotspot `(0,8)` and
364 changed reference pixels, which differs from the later standard arrow. A
second probe waits 12 seconds before comparing, verifies the standard arrow and
unchanged cursor/owned-window state, and finds an exact match at `(300,180)` with
hotspot `(0,0)`. The observations support a cursor-state mismatch in the first
comparison; they do not prove its cause. The CPU reference only checks pixels
already produced in the native frame and does not feed the display pipeline.

The archive preserves 68 remotely SHA-256-verified text files, totaling 3,144,846
bytes. It contains controller sources and metadata, with no captured desktop pixel
packets, binaries, model weights or compiled caches. Both cursor comparisons are
retained. See [raw-manifest.json](raw-manifest.json) and [research.json](research.json).

Next: qualify ordinary relative pointer movement, confinement to the displayed
logical desktop, and independent failure recovery. Then address response time,
long-duration stability, application rendering coverage, and matched Tahoma
performance. Tahoma was not tested in this run.
