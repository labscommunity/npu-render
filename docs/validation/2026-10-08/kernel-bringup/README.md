# Registered-adapter implementation groundwork

This report records a start-disabled kernel build and user-mode contract tests
on Delta. No kernel driver was installed on Delta, no registered graphics
adapter was enumerated, and no desktop work moved. The native kernel target is
`native/windows/kmd`; its README describes the remaining mandatory contracts.

The exact kernel image SHA-256 is
`73069ded48c5e11b0bddf3764b9a7298c0675c9ac19f7fcfb44d0099df5ae562`.
`kernel-image.txt` records the actual x64 native-subsystem image/import audit.
`kernel-build.json` records build scope. The `.sys` is deliberately not committed
or included in the desktop bundle.

`kernel-host-tests.log` runs the actual DDI source with pool/registration shims:
failed pool allocations at every position, malformed and overflowing surface
descriptors, duplicate destruction, cleanup, and rejection of adapter startup.
It does not test Windows kernel scheduling or IRQL.

`umd-contract-tests.log.gz` (the original log compressed without text conversion) includes the normal-loader ABI handshake rejection
cases, the portable 200,000-operation scheduler lifecycle check, and existing
explicit software-DDI CPU success/rejection cases. These loader tests do not
register an adapter or claim Direct3D conformance. The portable scheduler also
passed a local Clang AddressSanitizer/UndefinedBehaviorSanitizer run.

`npu-software-lab-regression.json` confirms the existing explicit software-DDI
renderer still runs its four small draw/oracle cases on the real NPU. This is a
regression check of the old route, not execution through the kernel adapter.

`source-sha256.json` was captured from the remote build inputs and checked
against the local files. The two root-level Python filenames in that report
were staged copies of the corresponding `tools/` scripts. Captured evidence
bytes are preserved by `.gitattributes` and `captured-sha256.json`.

An isolated Windows evaluation VM is being provisioned separately for actual
kernel registration tests. Its boot settings and test credentials belong only
to the disposable guest. VM progress is not kernel-execution evidence.
