# Bounded native-probe admission

An isolated Windows component passes **25 lease-shape checks, 11 permission
checks and five tests using separate child processes**. It provides a candidate
admission boundary for a single native graphics diagnostic. No graphics driver
was installed and no rendering device was exercised by these tests.

The controller creates a temporary, session-local mapping. Its fixed-size record
binds the controller, worker and one consumer to process IDs and creation times,
requires the same Windows session, limits the authorization to 120 seconds and
names a bounded local-pipe endpoint. The consumer retains process handles and
checks liveness, identity and expiration. Other process IDs, stale creation times,
expired records, unknown fields and malformed endpoint suffixes are rejected.

The mapping must be owned by Administrators or SYSTEM. Its access list must deny
unprivileged mutation; ordinary consumers may receive read and security-query
access. Missing/null access lists, unrelated owners and writable unprivileged
grants fail admission. These checks do not defend against a malicious administrator.
The controller is responsible for verifying the executable it launches before
authorizing its process identity.

The live process tests create owned suspended consumers, publish their exact
identities, and then resume them. One accepts the intended authorization; four
reject stale worker/controller creation times, expiration or a wrong consumer.
Owned child processes and mappings are released afterward. No registry, driver,
boot or system-security setting changes are needed for this component test.

Five remote artifacts were verified against SHA-256 and size receipts and
preserved losslessly under `raw/`. The exact source and clean-build script are
included. The positive tests run under the current administrator identity;
ordinary application processes, DWM's account and installed-adapter admission
remain separate qualification steps. This component is not a desktop switch.
