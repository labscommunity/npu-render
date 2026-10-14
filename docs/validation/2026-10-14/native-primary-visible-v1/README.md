# Primary desktop visibility and corrected alpha qualification

Delta, Windows 11, Panther Lake, 2026-10-14. **Full desktop offload remains
unfinished.** The controlled window now appears in a completed NPU primary-desktop
frame. A separate screen read detects its blue color beneath the physical
presenter. Native mouse/keyboard interaction has not passed: initialization takes
most of the finite test window, and the useful frame arrives just before expiry.
No Tahoma benefit or usable frame rate is demonstrated.

## What the missing-window investigation established

Live v16 removes `WS_EX_TOPMOST` from the owned input window and adds a virtual
primary-screen sample. Five completed frames still lack the blue window. However,
the immutable request capture contains its 131,072 blue pixels in requests 467 and
468. Request 467 is untextured; its existing desktop background already contains
blue at (70,70). This is evidence of content reaching an in-progress desktop
buffer, not a completed frame or display delivery.

Request 479 subsequently spends 91,506 ms in backend compilation and 94,133 ms in
compilation plus drawing. Its client leaves after the 90-second deadline. The
compositor restarts its request sequence, and request 484 fails with
`ZE_RESULT_ERROR_DEVICE_LOST`. This sequence is observed; a causal connection
between client departure and the device loss is not established.

The corrected alpha worker replays all 485 requests successfully. Outputs 466,
467, and 468 each contain the expected blue rectangle at (70,70), and match the
original successful output hashes. The replay field `controlled_blue_components`
counts four-byte pixel matches, despite its name. Recorded-output consistency is
not an independent rendering oracle.

Live v17 uses that qualified worker and the diagnostic driver update. It produces
828 acknowledged NPU operations and ten changing native frames during the finite
trial. Six frames reach the physical presenter before it exits. Frame 6 contains
the blue window at the expected source position, observed at 22:05:54 UTC. The
input child ends without receiving a click or key. Exact intermediate topology,
ordinary-window placement, cursor, foreground, and final physical-baseline
recovery all pass.

| Trial | Native rendering | Exact source frames submitted physically | Blue completed frame | Screen blue samples | Input |
| --- | --- | --- | --- | --- | --- |
| v16, worker25 | Fails after timeout and device loss | 5 | 0 | 0 | Fails; no input sent |
| v17, worker27 | Passes finite trial; 828 receipts and ACKs | 6 | 1 | 1 | Fails; useful frame arrives near expiry |

The screen observer checks presenter ownership **before** each `GetPixel` call.
Those calls can block for a long time, and timestamps precede the reads. Ownership
is not rechecked at completion. These observations do not measure visible latency,
physical-panel photons, or zero GPU use. Subsequent interaction qualification must
address this measurement limitation.

The v16 helper also exercises its physical-baseline recovery branch after the
owned indirect display disappears: the ordinary window, cursor, and foreground
are restored, while the failed exact intermediate-topology result stays failed.
This is one observed failure-recovery path, not comprehensive crash recovery.

## Corrected alpha candidate

Worker27 retains the exact integer alpha graph established in the
[previous archive](../native-primary-input-v1/README.md). Its eight integrated
CPU/NPU sampler cases pass, followed by these complete captured streams:

| Replay | Requests | Result |
| --- | ---: | --- |
| v23, earlier short failure stream | 117 | Pass |
| v24, compiler-failure stream | 256 | Pass |
| v25, larger desktop stream | 582 | Pass |
| v26, earlier primary stream | 477 | Pass |
| v27, primary stream containing the blue window | 485 | Pass |

Driver package16 plus worker27 passes registered Direct3D11 qualification with
eight actual NPU operations and eight ACKs. Package staging and both cleanup
transactions pass. This qualifies the recorded component and finite native trial;
it does not establish broad application compatibility.

## Diagnostic lifecycle correction

The compositor can retain the UMD between finite admissions. The old diagnostic
code retained an invalid admission and its prior file handle indefinitely, hiding
later compositor events. The update revalidates a fresh admission after the prior
one expires and reopens the log for the new generation. It preserves admission
checks and the four-MiB per-file budget; the line budget becomes 16,384 per
generation. Rendering math and the kernel binary are unchanged.

The lifecycle contract verifies two leases in one process, rejection of dead
workers/controllers and invalid replacements, a fresh log, and an unchanged
archived log. The first two test revisions fail because Windows rejects renaming
a directory containing an open file. The final test archives the shared-delete
file itself, then recreates the empty directory; all three admission/lifecycle
contracts pass. Both failed test logs are preserved. The actual v17 compositor
trace is present, and native cleanup succeeds.

Package16 version: `0.0.1316.0`. Signed UMD SHA-256:
`091878DA1B2A944CDAA2B41FC98F2477160C2E0EEE0B7782C6CC1DDA83275FE6`.
The lab package remains temporary and private.

## Remaining work

The subsequent bounded experiment increases primary hold time to ten minutes,
with a matching eleven-minute native monitor and independent thirteen-minute
recovery trigger. It must demonstrate blue → mouse → green → key → red through
completed NPU frames and independent screen observations before interaction is
accepted. A longer timeout is a diagnostic measure, not a responsiveness fix.

Concrete performance obstacles remain: v17 still encounters a 27.7-second cold
compile, and one draw requires 1,282 provider calls and about 11 seconds of drawing.
Preparation of a finite captured stream does not cover all future graph shapes.
Next work must reduce graph preparation and dispatch count, qualify long-duration
stability, implement ordinary pointer behavior and recovery, then cover application
graphics and repeat matched Tahoma measurements. See the
[acceptance plan](../../../research/desktop-offload-plan.md).

[Research notes](research.json) distinguish primary documentation from external
bug reports. A report on different hardware and models is not proof of Delta's
root cause. No external issue or message was posted.

## Evidence

All 248 raw files (5,169,580 bytes) were checked against remote SHA-256 receipts.
The [manifest](raw-manifest.json) records exact byte identities, and the
[report](report.json) asserts the stated outcomes. Raw Windows output is preserved
without line-ending conversion. Source patches and controllers are included;
desktop images, compiled graphs, binaries, and credentials are excluded.
