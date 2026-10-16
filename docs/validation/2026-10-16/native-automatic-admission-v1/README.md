# Normally started consumer admission: component qualification

The v2 component passes the v3 qualification on Delta. Two consumers started
normally, without suspended-process preauthorization, discover the broker,
request admission and validate their process-specific lease. Four requests with
altered identity or protocol fields are rejected. An unresponsive broker is
rejected after 3,032 ms; a stopped broker is rejected. Owned publications and
per-consumer leases are removed. The binary SHA-256 is
`53d466b49265f83853281e7ff94d2e8cd2bc94f3e7bb6daf5911aaca385bb556`.

This is a same-session, elevated, owned-process component using a dummy worker.
It makes no graphics or NPU calls and changes no driver. Ordinary application
rendering, standard-user and sandbox tokens, cross-session scheduling, lease
renewal and UMD integration remain unqualified.

The archive retains two preceding failures. V1 fails compilation under warnings
as errors because a `size_t` argument narrows to `DWORD`; v2 adds the explicit
bounded conversion. The first v2 PowerShell harness records failure because its
process exit-code property is null, despite successful component records. The v3
Python harness obtains the authoritative exit code, requires every expected
record and pins the binary before reporting success. Neither failure is replaced
or counted as a pass. No executables or private pixel images are committed.
