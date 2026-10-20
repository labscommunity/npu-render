# Combined renderer: exact replay and 6.81% aggregate improvement

Worker24 combines balanced geometry scheduling, ordered rectangle routing and
resident clipping. The selected 174-request replay and full 972-request replay
pass, preserving every worker19 image. The full replay observes 63 resident
clipping draws, 19 routed rectangles and the balanced geometry schedule.

Four complete A/B/B/A replays preserve all 3,888 images:

| Phase | Worker | Parent execution | Child execution | Replay elapsed |
| --- | ---: | ---: | ---: | ---: |
| A1 | 19 | 113,805.097 ms | 107,142.616 ms | 206.552 s |
| B1 | 24 | 105,940.339 ms | 99,334.251 ms | 197.133 s |
| B2 | 24 | 107,681.976 ms | 101,043.554 ms | 199.993 s |
| A2 | 19 | 115,439.444 ms | 108,700.763 ms | 211.093 s |

Across 1,944 pairs, parent execution falls from 229,244.541 to 213,622.315 ms,
**6.81% less aggregate time**; 837 individual pairs are slower. Time outside the
child falls from 13,401.162 to 13,244.510 ms. This meets the existing five-percent
promotion gate. Two runs per worker establish this finite replay result, not a
sustained desktop frame rate or a Tahoma benefit.

Both provider recovery cases pass: an injected exception and termination of the
owned child process. Each preserves thirty exact images and acknowledgments,
with two provider generations, one failure and one NPU retry. Failure occurs at
dispatch 13, original ticket 587, after the new routes have executed. Each case
observes nine clipping draws, two clipping imports, nineteen routed rectangles
and balanced geometry in both generations. Actual hardware device loss is not
induced.

The worker SHA-256 is
`03831470d266a0c0f6e095eb14124812f0899eedaa5c9fdffed063086675e2e4`.
The 138-file manifest retains exact source/build identities, selected/full
replays, all comparison requests and both recovery cases. Compiled binaries and
model caches are excluded. A separate registered/live desktop trial is required;
this worker result does not establish full desktop offload or GPU resource relief.

The [per-shader attribution](analysis/cost-by-shader.json) joins every dispatch
to its original ticket and whole-draw provider count. In the candidate replay,
the four largest families account for 17.94%, 12.15%, 11.96% and 11.66% of child
time. This is a different workload from the live animation; its ranking must
not replace the animation's measured attribution.
