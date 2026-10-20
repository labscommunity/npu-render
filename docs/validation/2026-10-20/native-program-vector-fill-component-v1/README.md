# Vector memory fill: qualified NPU component

Replacing the compact program kernel's byte-at-a-time `memset` with aligned
vector stores passes **448 exact NPU fixtures**, with the original carrier
checked before and afterward. Performance and desktop benefit are **unmeasured**
by this component test.

The existing target assembly contains six `memset` call sites and a scalar
`LSU1.STI.8` store loop. The replacement uses a scalar alignment prefix, sixteen-byte
vector stores and a scalar tail. Pixel/shader floating-point expressions are
unchanged. This inspection establishes generated instructions, not their
runtime frequency or fraction of rendering cost.

The new executable text is **18,240 bytes**, fitting the compiler-owned
18,256-byte region with sixteen bytes spare. The original carrier's task layout,
addresses and allocation metadata remain unchanged under the existing strict
code-only patch contract.

Candidate model SHA-256:
`00793de192c05fc47f58a7a41a184245ded4df14fd83b58a7a31a514df0121b2`.

Kernel source SHA-256:
`68340821c6a0d5ef90aca280ebd7478e3600b4890fc6f5eb3f0d85183bd4a52b`.

Before hardware execution, the isolated fill helper also passes **116,704 local
checks** over 32 starting offsets, 521 lengths and seven values, including
unchanged guard bytes and the returned pointer. That CPU helper uses a local
vector alias annotation; its result alone does not qualify the target kernel.
The separate hardware test compares every output span with its explicit oracle.

`artifact-sha256.json` records 29 artifacts, including target compilation,
original/custom/original checks, the local helper checks, and the baseline
assembly inspection. Large remote evidence files are compressed losslessly
where applicable, with both stored and uncompressed source hashes retained.
No compiled model or executable is committed.

The next experiment compares the pinned model inside the complete renderer,
with identical host rendering logic and turbo enabled for both sides. It must
preserve all recorded images before timing or desktop promotion.
