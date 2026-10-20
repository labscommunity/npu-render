# Owned checkpoint and NPU batch transaction

The C++ prototype connects the version ledger to the qualified V3 ordered
rectangle kernel on Delta. It owns an immutable copy of the committed pixels,
captures command state before execution, blocks reads of a pending target, and
publishes a new checkpoint only after the full NPU result is available. Commands
with explicit reads of old intermediate versions are rejected by this narrow
route; its consecutive target writes may therefore share intermediate storage.

Four 1024×768 scenarios pass, with full images compared against an independent
scalar integer reference:

| Scenario | NPU submissions | Model generations | Final version |
| --- | ---: | ---: | ---: |
| One 32-command RGBA batch | 3 | 1 | 33 |
| Two consecutive 32-command BGRA batches | 6 | 1 | 65 |
| Failure after partial output, restart and replay | 4 | 2 | 33 |
| Failure after partial output, abandon | 1 | 1 | 1 |

Caller pixels and command bytes are deliberately changed after preparation;
the admitted work still produces the original expected result. The failure cases
throw after one NPU submission has produced part of a surface. The committed
pixel hash and version remain unchanged and pending reads stay blocked. Recovery
reimports the pinned model, invalidates the earlier attempt, rejects its late
completion and recomputes the entire batch from the owned checkpoint. Abandonment
restores read access to the unchanged checkpoint. No CPU fallback is used.

This is an injected in-process exception after real NPU work, not actual hardware
device loss or child-process termination. The prototype is not connected to
Windows DDI callbacks, shared allocations, textured draws or presentation. It
provides transaction evidence for that next integration, not full desktop offload.

The first build failed because the global `Rectangle` alias collided with the
Windows API function, and the C++ wait API required a chrono duration. V2 fixes
those compilation issues without changing the test or kernel arithmetic. Both
sources and the failed build are retained. The passing executable SHA-256 is
`ab5162cc110fd8ed0483fb0c002c232b6552808028886c3b60a2982c6904863a`.
The 26-file manifest includes both builds, source hashes and all four receipts.
