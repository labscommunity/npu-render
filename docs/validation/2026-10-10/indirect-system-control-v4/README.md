# System-renderer monitor control

With the experimental renderer removed, Windows assigns the indirect monitor
and supplies desktop surfaces. A startup watchdog fires while our assignment
callback initializes Direct3D; the driver host recovers and receives more frames.
This motivates preparing the device before announcing the monitor.

This is a system-renderer control, with zero NPU or display-output claims. The
owned driver and package were removed. [report.json](report.json) records the
raw traces, debugger evidence and limitations.
