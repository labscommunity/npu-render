# Batching prerequisites: version contract and captured-resource inventory

The provider-independent command ledger passes its CPU tests under macOS Clang
with address/undefined-behavior sanitizers and on Delta with MSVC `/W4 /WX`.
Tests cover 10,000 modeled batch lifecycles plus stale/future references, immutable
command-state capture, partial completion, retry, retirement, generation reuse,
and byte/sequence limits. Its CMake test is included in normal repository CI.
This code owns metadata, not texture storage, and is not connected to Windows
rendering callbacks. No NPU execution or performance gain is attributed to it.

The independent inventory tool decodes all 972 original packets with the qualified
protocol implementation. Every packet byte count and SHA-256 matches the pinned
capture index. It exports shader identifiers and resource shapes, formats and
samplers; no pixel data is exported. There are 25 groups including clears, and
4,780,282,804 captured packet bytes. The first Windows build failed on an unavailable
`MAXULONG` macro. The retained v2 changes only that limit to the standard C++
`numeric_limits<ULONG>::max()` expression and adds its explicit header; it builds
and passes the inventory checks.

This narrows the next batching implementation. The complex five-sample shader's
ten recorded draws each reference 4,374,528 texture bytes, including a
1024×1056 surface. The common modulated-texture family ranges from 1,216 to
28,108,800 bytes. These resources cannot simply be copied whole into the existing
262,144-byte per-core kernel span. Tiled residency, explicit resource dependencies
and/or a separately qualified larger carrier are needed for that strategy.

Captured hashes do not establish allocation identity. This inventory does not
infer resource versions, frame dependencies, readback boundaries or supported
batch groupings. Windows map/copy/flush/present ordering and owned storage must
still be implemented and tested. The 128-file manifest retains both inventory
attempts, pinned decoder headers, Windows build/test records and local sanitizer
evidence. Full desktop offload and Tahoma improvement remain unproven.
