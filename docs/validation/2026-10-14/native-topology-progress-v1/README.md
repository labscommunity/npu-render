# Native topology and continuous-frame investigation

Full desktop offload remains unfinished. Native trial v8 delivered three different
1024×768 frames through the NPU-backed indirect display. Windows had automatically
cloned the physical desktop. A separate, explicitly extended layout applied in
trial v9, but the compositor subsequently exited after a rejected clear. That
trial **failed**, despite an incorrect success flag in its original controller log.

## Qualified observations

- Trial v8: first frame at 70.784 seconds after admission; subsequent observations
  56.391 and 23.395 seconds apart. Final worker logs contain 399 NPU receipts and
  399 delivery acknowledgments. The controller observed 398 before cleanup began;
  one additional request completed during teardown. These are still unusable
  frame intervals, and there was no scripted animation or matched Tahoma load.
- The read-only observer recorded 103 topology samples without failure. The
  physical path before and after admission matched in every reported field.
- Windows' ordinary display-query view reported both cloned targets against the
  physical source. A virtual-aware query exposed distinct physical and indirect
  adapters, with both logical sources at `(0, 0)` and 2880×1800.
- The virtual-aware helper validated and applied an extension: physical display
  2880×1800 at `(0, 0)`, indirect display 1024×768 at `(2880, 0)`. The physical
  source, target, position, resolution and reported refresh remained unchanged.
- The compositor then exited. Its last rejection was a clear operation; the old
  trace did not include that clear's dimensions or exception text. A large-clear
  size limit is a hypothesis for the next investigation, not a proven diagnosis.
- Recovery removed the temporary devices and certificate trust and preserved
  memory integrity. The extension helper's attempted restoration of the previous
  two-display clone returned error 87 because the indirect display had already
  been removed. The final physical-only configuration matched the baseline.

## Correcting the controller's success result

The bootstrap set `passed=true` when it first saw a valid frame. Its exception
handler did not clear that flag if a later compositor check failed. Accordingly,
the original v9 bootstrap and outer report contain misleading success values.
Preserve those original logs as evidence; use the effective failure verdict in
[report.json](report.json).

The v5 bootstrap candidate explicitly sets `passed=false` on later failure. A
regression check executes the old and fixed exception handlers against a simulated
post-frame compositor failure: the old handler incorrectly passes, while the new
handler fails and retains the earlier frame evidence. The fix is not yet a
qualified native desktop run in this archive.

## Harness issues resolved during investigation

Delta's unlocked session was displaying the ASUS OLED Care screen saver. Ordinary
simulated mouse input did not dismiss it. Windows reported that it was nonsecure;
the exact Microsoft-signed OEM screen-saver process was dismissed. No screen-saver,
login or security setting was changed. Subsequent observation held a temporary
display-awake request and released it on exit.

The first topology controller assumed at most 64 possible paths; Delta returned
68 before indirect admission and 69 afterward. A later candidate uses bounded
larger arrays and logs the required sizes. A PowerShell argument-construction bug
also split `--inspect` into two arguments; the corrected wrapper passes one.
Finally, virtual-aware queries and matching mode-index union fields were required
to configure this virtual clone correctly.

## Reproduction and limits

The archive contains 137 remotely SHA-256-verified text artifacts, candidate
topology/presentation sources, controller sources and the failure-reporting
regression result. Raw artifacts are deterministically compressed under `raw/`;
[raw-manifest.json](raw-manifest.json) records original and compressed hashes.
Desktop pixels and captured draw buffers remain outside version control.

The renderer package was still v8 / 0.0.1308.0, with native-routing-v10b. The native
renderer source was unchanged during these topology experiments. Temporary
observation leases remained finite, and the independent recovery task was armed.
These results do not establish a live physical presentation bridge, native input
integration, reliable extended-desktop rendering, application graphics migration,
or a Tahoma speedup.

Microsoft's [CCD example](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/ccd-example-code)
describes virtual-aware display paths and how source positions determine virtual
cloning. The helper uses temporary supplied configurations and omits persistence
to the display database.
