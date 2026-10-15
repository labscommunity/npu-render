# Scoped desktop integration: delivery failure detected

This trial **does not qualify sustained rendering or full desktop offload**.
The original live controller reports success, but its relay fails with Windows
error 5 while replacing the published frame. It omits relay health from its
success condition and continues displaying the last frame. Raw reports are
preserved unchanged; the derived report rejects that false positive.

Before the failure, 31 physical submissions match their native source frames
exactly. Final logs contain 1,403 acknowledged operations and matching NPU child
receipts; the last incremental bootstrap snapshot contains 1,398. There are no
recorded provider faults. Mouse and keyboard responses reach the physical output
after 29.34 and 25.32 seconds, respectively. The initial response is held until
the observer sees it, preventing later animation from impersonating the response.

The first and last submissions are only 388.60 seconds apart despite a 525-second
presenter lifetime. A new notification wait works, but does not prevent this
publication failure. Most residual GPU counter samples were collected after
delivery stopped; they cannot establish the cost of a continuously updating
bridge or capacity freed for Tahoma. There is no matched control or Tahoma run.

The earlier long-trial failure is also retained: the indirect display host rejected
a 38-minute lease because its header still imposed a 15-minute limit. IDD v2
aligns that limit with the already qualified 40-minute desktop ceiling, while
keeping the ordinary application limit at two minutes. Four contract suites pass
in each build configuration. This archive does not prove long-duration stability.

All 218 raw files are checked against remote SHA-256 receipts. Sources and patches
have a separate manifest. The next acceptance check must require a healthy relay,
successful relay termination, continued source-to-presentation correspondence and
a fresh delivery near the end of the requested interval, with independent output
observation. Physical display scanout and bridge GPU copies remain.
