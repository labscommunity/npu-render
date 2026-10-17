# Repeated-input and physical-observation tools

Both tools build with MSVC `/W4 /WX` and pass their bounded Windows qualification.
The input tool owns its window, uses tagged mouse and Unicode-key injection,
rejects unsolicited messages, checks focus and foreground ownership, and restores
the prior cursor and foreground window. Its contract run checks eight events,
including four mouse and four keyboard deliveries, plus unsolicited-message
rejection. This contract run does not establish native NPU rendering.

Observer v5 retains the existing ownership checks and exposes the sampled BGRA
bytes only when the presenter owns each point before and after readback. Its
qualification checks three exact colors and rejects a different owner, returning
zero sample bytes in that case. The qualification issues fourteen GPU copy
commands; it is an observation tool, not evidence that GPU work has disappeared.

The repeated-input analyzer joins unique stimulus colors to exact published and
submitted frames, matching display counters, and independently owned pixel reads.
Its revised version accepts an acquisition that begins before input and completes
afterward. Nine synthetic evidence-join checks pass. The earlier analyzer and its
checks remain available as history.

The planned live measurement contains forty alternating inputs and a ten-second
30 Hz color animation. It counts missing submissions and producer skips, and
counts displayed updates only inside the scheduled interval. Tool qualification
does not establish its live response distribution or animation rate.

The [manifest](artifact-sha256.json) covers 44 artifacts, including source,
Windows reports, build hashes, and synthetic analysis checks. No Tahoma test ran.
