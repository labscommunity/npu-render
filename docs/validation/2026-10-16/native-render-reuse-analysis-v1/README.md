# Retained-request reuse opportunity

A read-only analysis hashes all 972 retained requests after removing only their
transport nonce, sequence and client PID. All rendering fields and the complete
body remain in the key. It reads 4,780,282,804 bytes and finds 587 distinct inputs:
385 requests duplicate an earlier normalized input. Every duplicate group has
one consistent worker-v18 output hash.

This does **not** establish a useful cache. Simulated LRU capacities of 64, 128
and 256 MiB produce only 198, 199 and 207 hits respectively. Summed prior B1 child
time on those hypothetical hits is 3.716, 3.717 and 3.725 seconds out of 110.120
seconds total. Those are opportunity estimates before hashing, validation and
copying costs, not measured savings. Current animation inputs were not captured.

No cache or renderer change is implemented. The result does not justify adopting
a cache as the main responsiveness fix. A future implementation would need exact
byte comparison, complete request validation, explicit NPU-result provenance,
bounded memory and invalidation when renderer or provider identity changes.

The [manifest](artifact-sha256.json) covers the analysis, its full request/group
report and collector. No model, GPU command or NPU inference ran in this analysis;
the timing values come from the separately retained worker comparison.
