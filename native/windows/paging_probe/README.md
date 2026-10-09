# Isolated Windows paging probe

This uses the public KMT entry points in `gdi32`, without a replacement UMD or
OpenVINO. `--list` queries adapters without creating graphics resources.
`--exercise` additionally requires the owned disposable guest's computer name
and volume label, exactly one matching NPU Render kernel identity, and
`ready = 0`. It creates a device and 12 KiB allocation, requests non-evicting
write/read locks, verifies a byte pattern, and checks cleanup results. It never
selects a default, foreign, or WARP adapter after a failed identity check.

Build on Windows with MSVC and the Windows SDK:

```powershell
cmake -S native/windows/paging_probe -B build/windows-paging-probe -A x64
cmake --build build/windows-paging-probe --config Release
build/windows-paging-probe/Release/nr_paging_probe.exe --list
```

Only run `--exercise` in the isolated kernel-debug guest with the explicit
`--lab-paging-adapter` driver. Capture stdout and exit status. API results are
JSON lines, flushed before continuing so failed operations remain observable.
A successful lock round trip alone does not establish paging DMA or scheduler
completion. Correlate it with the owned driver's kernel trace. Neither result
establishes NPU execution, rendering, desktop offload, or Tahoma benefit.
