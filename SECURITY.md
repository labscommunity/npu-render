# Security

This repository holds a research prototype that installs a test-signed display
driver, an indirect display and a scheduled recovery task on a Windows machine.
Run it only on hardware you own or are authorized to test on, with test-signing
mode on, and expect the compositor to be restarted by the recovery task.

## Reporting a vulnerability

Report security problems privately through GitHub's private vulnerability
reporting for this repository (Security tab, "Report a vulnerability"). Do not
open a public issue for a security report. Include the build or package version
(`npu-render desktop status` prints the pins), the Windows and NPU driver
versions, and the trace or dump that shows the problem; the per-process traces
under `C:\ProgramData\T8-NpuRender-NativeTraceV1` contain draw metadata only,
never surface pixels.

## What is never committed

Model weights, compiled device caches and carrier blobs, the SHAVE toolchain,
credentials, lease secrets, private machine addresses and virtual environments
stay out of the repository; CI and the commit hooks enforce the message policy
and the Python checks, and reviewers enforce the rest.
