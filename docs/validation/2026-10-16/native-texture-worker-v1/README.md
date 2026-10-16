# Integrated three-texture worker qualification

Worker v4 passes all 972 captured requests with one NPU provider generation and
zero provider failures. It admits all 24 four-sample draws, five solid rectangles
and nine three-texture draws. Twenty-eight additional solid attempts explicitly
continue through the existing NPU implementation.

The replay matches 962 original pixel references and the nine independently
qualified precision corrections. Ticket 971 has no original reference. A separate
audit compares every output with worker v3: 963 remain identical, including ticket
971; exactly the nine qualified corrections change. Original captures and their
index remain unchanged. A pinned manifest validates the exact request, old pixel
hash, corrected pixel hash, independent graph hash and byte count for each
exception. Nine malformed-reference tests reject their inputs.

The 28-request selected replay passes first. Registered Direct3D qualification
v27 subsequently passes and restores the registered adapter and certificate state.
Windows memory integrity remains enabled. These tests do not establish live
desktop usability, general application coverage or Tahoma benefit.

The worker SHA-256 is
`55e04a91b6bf7cbecd9c081733f6c4fbacc578864ed180dfb493263eff2af03f`.
The precision correction manifest SHA-256 is
`5afd2995cb49f64f3bb6d7d3ce75e30b1fb7d6f989261027425cd76768d70c48`.
The specialization validates the exact pixel shader, resource/sampler layout and
model hash. Its eight-entry cache declines explicitly to the existing NPU path
when full; that path retains the same coefficient correction. There is no CPU
rendering fallback in this qualification.

See the [independent corrected comparison](../native-three-texture-correction-v1/README.md)
and [covered-fragment compaction](../native-three-texture-compaction-v1/README.md).
The manifest preserves source, build, selected/full replay, independent audit and
registered-driver recovery evidence. Model blobs, executables and pixels are
excluded.
