# Repeated desktop input and animation, worker v18

The finite v55 desktop trial passes 1,680 acknowledged NPU operations,
103 exact physical submissions, all forty controlled input colors, the ordinary
default-adapter application, and restoration. There is one provider generation,
no fault or retry, and memory integrity remains enabled. **Responsiveness fails
the acceptance target.**

| Measurement | Mouse | Keyboard |
| --- | --- | --- |
| Warm received-input → matching-display median | 97.552 ms | 97.149 ms |
| Warm received-input → matching-display p95 | 116.208 ms | 109.503 ms |
| Warm sent-input → matching-display p95 | 3,436.587 ms | 109.764 ms |
| Warm received-input → independent-read completion p95 | 255.554 ms | 223.295 ms |

There are twenty inputs of each kind. Warm statistics exclude the first input of
each kind and contain nineteen samples, so nearest-rank p95 equals the maximum.
Mouse input 15 spends 3,320.379 ms between sending and application receipt; measuring
only after receipt would conceal this stall. Its interval overlaps 2,201.947 ms
of child rendering. This is overlap evidence, not proof of exclusive causation.
Display counters are matched to submitted frame counters; they do not measure
panel photons. Independent pixel-read timing includes observation cadence.

The ten-second animation generates all 300 scheduled updates with no producer
skip. Only nine distinct colors are submitted and display-counter verified,
including the drain interval. **Seven appear within the scheduled ten seconds:
0.7 verified updates per second.** The same interval contains seven publications
and seven submissions of any content, locating the main delay before publication.
This is a short owned-window color test, not a whole-desktop frame-rate guarantee.

The [animation attribution](analysis/animation-rendering-v1.json) covers 207
overlapping rendering requests and 3,361 whole-request provider calls, with zero
backend compilation time. Child execution occupies 6,599.588 ms; packet/parent
stages occupy 1,711.431 ms and time outside serialized parent requests 1,688.981 ms.
Child execution includes host work and NPU waits, not just device time. Shader
groups `b7f4e6d0…`, `c71638aa…` and `595296ba…` contribute 1,547.426, 1,457.600 and
1,338.821 ms respectively. Request counters crossing an interval boundary are
whole-request context, not apportioned counts.

The separate initial legacy mouse and keyboard events complete physical reads in
3,166.481 and 94.004 ms. These remain uncontrolled cross-run observations and do
not establish improvement over v54. The first coordinator stopped at its prior
analysis prerequisite; v7-v2 uses the retained corrected prerequisite and passes.

The [manifest](artifact-sha256.json) covers 173 artifacts, including the failed
predecessor, actual controllers, full live logs, forty-event measurements, and
local interval analysis. Remote artifacts were checked against Delta's SHA-256
receipts. Physical GPU copies and scanout remain. No GPU-relief or Tahoma trial ran,
and continuous arbitrary-application admission remains unqualified.
