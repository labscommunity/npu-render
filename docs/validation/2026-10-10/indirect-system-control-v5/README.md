# Prepared-device system-renderer control

Windows supplies five 1024×768 BGRA8 desktop surfaces after the VM display wakes.
Device preparation takes 422 ms and completes before monitor announcement.
Both 45-second checks preserve the original and indirect display paths. No
watchdog break was observed during this run. The owned driver is removed cleanly.

Windows’ system renderer produces these frames; the diagnostic sink displays
none of them. This validates monitor delivery, not NPU rendering or desktop
offload. See [report.json](report.json) and the preserved raw traces.
