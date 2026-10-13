# Reject loop-test invocations that select no cases

`nr_registered_loop_test --backend NPU` previously treated `--backend` as the
backend and `NPU` as an unrecognized section, returning zero without running a
case. The test now validates backend, section, argument count and packet-output
options before executing any provider work.

A clean x64 Windows build on Delta passes the full CPU loop contract and a new
CTest contract covering six invalid invocations. Each invalid case must return
exactly 1, produce no stdout and report the explicit argument rejection on
stderr. A crash, timeout or unrelated loader error cannot satisfy this contract.
The tested source hashes match the three changed repository files. Renderer
execution and arithmetic are unchanged. Repository Ruff checks also pass.

The earlier empty hardware-test log remains preserved in the input-reuse
experiment, alongside its separate corrected 77-result NPU qualification.
