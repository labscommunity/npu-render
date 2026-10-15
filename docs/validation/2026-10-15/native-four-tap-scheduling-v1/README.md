# Four-sample scheduling and recovery investigation

These are isolated NPU component experiments on Delta, not a live desktop or
Tahoma benchmark. No desktop topology, installed driver or OS power policy changed.
The byte-for-byte evidence manifest includes failed candidates.

- Caching the four helper coordinates per two-by-two pixel group preserves all
  4,194,304 independent reference components and all three captured draw outputs.
  Warm complete replay takes 1,637.20 and 1,616.49 ms, versus 9,340.22 and
  9,231.10 ms for the existing NPU renderer. Both candidates still use 50 calls.
- Reference-software Gather branches all landed on one tile. Default-hardware
  Gather graphs distributed work but used DMA for sampling; their SHAVE ABI is
  index conversion, so those graphs were not substituted with a sampler kernel.
  Batched Gather variants also failed to provide the required sampler contract.
- Six shared requests overlapped correctly but took 890.99 ms versus 383.45 ms
  serially for the first group. Overlapping six separately imported models lost
  the NPU device, followed by a ze_loader.dll access violation during teardown.
  That candidate is rejected. The Windows application fault and partial output
  remain in the raw evidence.
- A fresh process subsequently passed 1,048,576 exact original Gather components
  without a reboot or manual device reset. This establishes recovery for that
  experiment, not a general concurrency or production recovery guarantee.

The next candidate uses six compiler-owned spans inside one graph. Its ABI,
content integrity and actual draw performance require separate qualification.
See [the acceptance plan](../../../research/desktop-offload-plan.md) for the
remaining full desktop and Tahoma gates. Compiled graphs, binaries and captured
private pixels are excluded; source snapshots and reports are hash verified.
