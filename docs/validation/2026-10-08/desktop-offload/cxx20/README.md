# C++20 capture build follow-up

Source commit `979ebe391f2ccf6012b01456cdd54f23816397f3` changes only the four
Windows capture targets to C++20. Visual Studio 2026 CI rejects the former
experimental coroutine path used by C++17 capture headers. The standard-coroutine
build passes [all CI jobs](https://github.com/t8/npu-render/actions/runs/37831833247).

Delta build `desktop-offload-v18` passes all 262 interaction/lifecycle checks and
zero NPU dispatches while disabled. Suspending the owned UI thread triggers
watchdog removal in 2.110 seconds. Source and binary hashes are retained here;
all source hashes were checked against the recorded commit. The separate WDDM
allocation probe still uses its previously qualified C++17 build.

The original-desktop equivalence failure remains: 297975 pixels differ by more
than 2 bytes, with maximum error 41. Output delivery still matches the NPU image
within one byte. This build fix does not establish full desktop offload or a
Tahoma benefit; the parent directory documents the remaining integration gaps.
