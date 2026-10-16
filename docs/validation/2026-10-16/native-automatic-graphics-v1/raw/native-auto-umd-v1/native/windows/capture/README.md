# Captured-window desktop experiment

This host captures individual visible windows, composes their BGRA8 pixels on an
explicit OpenVINO backend, and presents an opaque, click-through desktop-sized
window. Source applications remain alive and receive input underneath it. The
host releases capture sessions when disabled; closing or terminating it reveals
the original desktop. An external watchdog restores the original desktop if the
host stalls. It does not change the installed display driver.

**This is not full desktop offload.** Client rendering, GPU capture and output
transfers remain. Capturing individual windows also omits some Windows effects:
the full-image comparison against the original desktop fails around shadows and
the taskbar backdrop. The successful output oracle checks delivery of our NPU
image; it does not establish equivalence to Windows' original image. The native
report records these separately and always reports `full_desktop_offload: false`.
No Tahoma improvement has been established for this path.

## Run and recover

Build from a Windows checkout with Visual Studio C++ tools and the Windows SDK:

```powershell
uv sync --frozen --group native
uv run --frozen --group native python tools/build_native.py
uv run --frozen python tools/run_desktop_capture.py --executable build/native/Release/nr_desktop_offload.exe --seconds 60 --report results/desktop-capture.json
```

Use **Ctrl+Alt+Shift+F11** to disable/re-enable and **Ctrl+Alt+Shift+F12** to exit.
Run from a connected `WinSta0\Default` desktop. The supervisor is the supported
experiment launcher; its separate process detects a stuck UI thread. An initial
preparation failure leaves the existing desktop available. No startup entry or
system graphics setting is changed.

The current contract requires one SDR, 8-bit primary monitor attached to the
selected D3D11 adapter. It rejects reported capture exclusions, inaccessible
desktops, oversized source sets and changed display dimensions. HDR, multiple
monitors, protected sources, secure desktops, arbitrary application fidelity and
general Windows shell interaction coverage are unqualified. A dynamic source that cannot be
captured causes the output to hide/exit, rather than silently substituting pixels.
Source geometry, capture and window enumeration are not one atomic OS snapshot.

`--calibrate-system-overlay` is a qualification option for a machine with a small
persistent OS overlay, such as Delta's activation watermark. It briefly displays
two uniform frames and measures an affine per-channel overlay model for the
screen oracle only. The overlay remains visible. This calibration must not be
used to claim ownership of the whole display or equivalence to the original
Windows desktop.

## Qualify

```powershell
uv run --frozen python tools/validate_desktop_capture.py --executable build/native/Release/nr_desktop_offload.exe --fixture build/native/Release/nr_capture_probe.exe --backend NPU --popup-cycles 100 --report results/desktop-functional.json
uv run --frozen python tools/validate_desktop_watchdog.py --executable build/native/Release/nr_desktop_offload.exe --report results/desktop-watchdog.json
```

Add `--calibrate-system-overlay` to these commands when needed on Delta. The
fixture exercises input, updates, moves/resizes, popup destruction and hiding,
minimize/restore, disabling, re-enabling, clean exit, forced process death and
invalid-backend rejection. It does not cover every existing Windows application.
The watchdog check suspends only the newly launched host's verified UI thread and
requires the supervisor to terminate that owned child and remove its output.

The native `--compare-original true` option assumes a stationary initial desktop.
It records full-image differences against a pre-capture screen snapshot. Those
differences do not currently gate the component-level `passed` field; consumers
must not interpret that field as a full-desktop acceptance result.

## Costs and correctness

Per-window textures are copied through a GPU staging texture and mapped on the
CPU. Host buffers are packed into a fixed 1024×512 NPU blend graph. Fully opaque
coverage removes hidden lower-layer spans; partial alpha remains in the graph.
The provider commits destination spans only after all required dispatches finish.
A cached root image and dirty-window detection avoid unnecessary recomposition.
Final pixels are uploaded for D3D11 presentation. The reports count our explicit
readback/upload/tensor bytes, not all driver or system traffic.

The screen check uses bulk readback and compares every screen pixel except a
bounded cursor neighborhood. Independent CPU source-over and provider oracles
check arithmetic separately. Capture session retirement occurs off the UI thread
with a deadline: source destruction can otherwise deadlock a synchronous capture
close. The external watchdog remains necessary for unexpected blocking calls.
