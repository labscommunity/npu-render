# Packet-forwarding worker desktop trial

The finite v56 trial passes 1,682 acknowledged NPU rendering operations,
103 exact physical submissions, forty controlled input colors, the eight-operation
ordinary default-adapter application, and restoration. There is one provider
generation, no fault or retry, and memory integrity remains enabled.
**Responsiveness still fails the acceptance target.**

| Warm measurement | Mouse | Keyboard |
| --- | --- | --- |
| Sent-input → matching-display median | 94.649 ms | 89.572 ms |
| Sent-input → matching-display p95 | 107.663 ms | 1,632.388 ms |
| Received-input → matching-display median | 93.681 ms | 89.243 ms |
| Received-input → matching-display p95 | 106.757 ms | 1,632.005 ms |
| Received-input → independent-read completion p95 | 221.627 ms | 1,744.122 ms |

Each kind has twenty inputs. Warm statistics exclude its first input; with
nineteen samples, nearest-rank p95 equals the maximum. The first mouse response
takes 1,908.743 ms from send to matching display. Display counters do not measure
panel photons; independent read timing includes observer cadence.

The ten-second animation emits all 300 scheduled updates, with no producer skip.
Nine distinct updates are submitted and display-counter verified including drain;
**seven appear during the scheduled interval, or 0.7 verified updates per second**.
This matches the preceding worker's visible update rate. Separate finite trials
and differing redraw stalls do not establish an overall desktop performance win.

The [animation attribution](analysis/animation-rendering-v2.json) contains 210
overlapping rendering requests and 3,442 whole-request provider calls, with zero
backend compilation time. Child execution occupies 6,839.935 ms, other serialized
parent/child stages 1,274.863 ms, and time outside those requests 1,885.202 ms.
Only seven publications and seven submissions occur in the interval, placing the
main delay before publication. Child execution includes host work and NPU waits.
Boundary-crossing request counts are whole-request context, not apportioned work.

The [manifest](artifact-sha256.json) covers 173 artifacts: controllers, device
logs, input and physical-display measurements, restoration, and local interval
analysis. Remote files match Delta's SHA-256 receipts. The isolated ABBA replay
shows a small aggregate packet-forwarding benefit, but this live trial does not
show improved animation. Physical GPU copies and scanout remain, and no matched
Tahoma or GPU-relief measurement ran. Full desktop offload remains incomplete.
