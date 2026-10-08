# Kernel debug symbols

The optimized start-disabled kernel target now emits full CodeView/PDB debug
information. Delta's x64 MSVC build and actual-source host DDI contract test
passed. The original build log is compressed without altering its bytes.

`build-report.json` records the unsigned kernel image and matching PDB hashes.
`source-sha256.json` identifies the changed build inputs and kernel source.
`captured-sha256.json` covers the captured report, log and source manifest.

This adds debugger visibility; it does not enable `StartDevice`, install a
driver, or validate kernel execution. A separately test-signed copy has a
different file hash; its CodeView record must still match this PDB. The prior
kernel bring-up captures retain their original, different binary identity.
