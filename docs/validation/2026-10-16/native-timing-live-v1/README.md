# Input-to-output attribution in the live Windows desktop

Worker v7 passes native live v42 and restores ordinary Windows state. The
presenter submits 48 matching frames over 538.230 seconds, with 30 distinct source
hashes and four fresh physical-output observations near completion. The desktop
worker acknowledges 1,190 NPU operations with one provider generation, no faults
and no retries. It admits 535 shared geometry, 29 masked-texture, 25 four-sample,
five solid and one three-texture draws.

The controlled mouse response takes 8,238.579 ms. The updated frame is published
after 8,167.773 ms and first observed on the owned physical output 70.805 ms later.
The serialized child renderer is busy for 6,859.287 ms during that interval,
across 81 requests. The parent and child timestamps partition the delay:

| Interval | Time overlapping the mouse response |
| --- | ---: |
| Parent work before child connection | 173.440 ms |
| Child packet reception | 140.749 ms |
| Child admission and queue entry | 0.492 ms |
| Child rendering, compilation and orchestration | 6,859.287 ms |
| Parent work after child rendering | 343.823 ms |
| Outside the serialized parent intervals | 720.788 ms |

The largest shader contribution is `595296ba…`: ten requests overlap by
2,159.846 ms, with 288 whole-request provider calls and 1,083.822 ms of
whole-request backend compilation. `c71638aa…` contributes 1,345.286 ms without
backend compilation. The recently optimized masked shader contributes only
143.082 ms. This trace explains why optimizing that component alone did not make
the mouse response fast. It identifies rendering as the principal delay in this
event, rather than inferring a bottleneck from component timings alone.

The keyboard response takes 187.848 ms: 101.113 ms until frame publication and
86.735 ms from publication to first observation. Two child requests occupy
64.230 ms. [The analysis](latency-analysis.json) retains all contributing request
identities, source hashes and timestamp calculations. It is derived from final,
hash-preserved logs, not the earlier active snapshots.

These are two controlled events, not p95 latency or a matched A/B. Child intervals
include host orchestration and NPU waits; they are not hardware utilization.
Whole-request compilation and call counts are context for partially overlapping
requests. Observation includes sampling cadence and is not panel photon timing.

Mouse, keyboard, relative motion, confinement, pointer reacquisition and recovery
pass. The presenter issues 96 GPU copies, including 48 validation readbacks; the
observer adds 142 measurement copies. These counts do not establish GPU time or
power savings. Physical scanout still uses Intel display hardware. Ordinary
application coverage, responsiveness, this renderer's thirty-minute gate and
Tahoma benefit remain open.
