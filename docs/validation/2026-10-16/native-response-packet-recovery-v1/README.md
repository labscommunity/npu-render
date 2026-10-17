# Response-packet worker recovery

Worker v19 passes two bounded fault scenarios on Delta: an injected provider
exception and termination of its owned provider child, each at dispatch five.
Each scenario completes twenty requests, preserves every worker-v18 output,
acknowledges every delivery, and records two provider generations, one failure
and one retry on the NPU. No CPU fallback is enabled.

The scenarios use the same worker binary as the
[972-request qualification](../native-response-packet-worker-v1/README.md).
These are synthetic provider-failure checks, not physical NPU device-loss,
sleep/wake or sustained desktop qualification. No desktop or Tahoma workload ran.

The first coordinator stopped at an unmet predecessor gate before fault testing.
Its failure is preserved alongside the successful v2 coordinator and both actual
scenario logs. The [artifact manifest](artifact-sha256.json) records all 32 files;
remote files were checked against Delta's SHA-256 receipts before curation.
