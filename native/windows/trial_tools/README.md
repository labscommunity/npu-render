# Trial tools

The three native tools the live trials and the desktop switch run beside the
NPU-rendered desktop. They are harness, not product: each one measures or
guards one thing and ends when told to.

- `frame_present_probe`: reads the relay's verified frames and presents them
  on the physical panel (`--live`), and provides the finite input stimulus and
  input contract used by trials. Built on Delta as `native-present-v24`.
- `topology_control`: establishes the indirect display as primary with the
  physical panel beside it, optionally confines the pointer for the trial, and
  restores the topology and window placements. `native-topology-control-v15`.
- `output_observer`: samples two physical-panel pixels through desktop
  duplication and reports whether the owned presenter is what the panel shows.
  `native-output-observer-v7`.

`session_gate.hpp` and `live_topology.hpp` are identical across the tools; the
Delta build consumes one flat directory per tool, so the copies stay in each
tree. Since these versions every tool waits out a desktop switch (screen saver,
lock screen, secure prompt) through `wait_console_default` and logs
`desktop_departed` and `desktop_returned` rows instead of ending the trial; the
presenter treats an occluded present as a skipped frame and the observer
re-creates its duplication after access loss.

Build and qualification on Delta: `tools/delta/trial-tools/` holds the
per-version build scripts (they extract `vm\<name>.tar.gz`, verify the source
hashes in `vm\<name>-source.json`, build with MSVC and write
`builds\<name>\build-report.json`), the qualification scripts the generated
trial scripts check, and `build_and_qualify_v24.ps1`, which runs the whole
chain. The sources here are the ones hashed in those build reports.

## Interactive admission broker

`interactive_broker` is the automatic admission broker (`nr_auto_interactive_broker`),
its consumer fixture and the lease contract test. It is packaged with the driver's own
`native_probe_lease.hpp` and `native_auto_admission.hpp` (copied as `admission.hpp`) from
`native/windows/registered_lab`, so the headers are not duplicated here. One-shot mode
admits one cued client, as the registration qualification exercises; `--serve <stop file>`
keeps the session's publication current and admits every same-user interactive client for
the admission's lifetime, renewing each 120 s lease in place. Build `native-auto-interactive-v8`.
