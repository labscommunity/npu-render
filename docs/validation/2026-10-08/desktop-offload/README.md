# Desktop capture and real shared memory on Delta

**Full desktop offload remains incomplete.** These experiments establish two
components: interactive NPU composition of captured windows, and a lower-level
cross-process memory handoff. Neither provides a Windows graphics adapter that
DWM can select. There is no new Tahoma benefit result.

Hardware: Panther Lake Core Ultra X7 358H, Arc B390, Windows 11 build 26200,
OpenVINO 2026.1.0, loaded NPU driver 1005540. Installed driver versions and boot
identity are retained in `source-binary-manifest.json`. That manifest records the
SHA-256 of every new experiment source/dependency and the exact tested binaries;
the source hashes were checked against this checkout. Binaries were built in
separate versioned directories; existing application launchers were not replaced.

## Captured-window host

Final build: `desktop-offload-v17`. Reports: `desktop-functional-16*`,
`desktop-watchdog-02*`, `desktop-supervisor-clean-02*`.

| Check | Observed result |
|---|---|
| Physical desktop | 2880×1800, one SDR 8-bit output, connected Default desktop |
| Independent blend arithmetic | Maximum channel error 1 byte; execution NPU, driver 1005540 |
| Output image vs screen readback | 5,167,871 pixels compared, maximum channel error 1 byte |
| Original Windows image vs NPU image | **Fails equivalence**: 297,965 pixels differ by more than 2 bytes; maximum error 42 |
| Input and window lifecycle | 262 checks pass, including 100 popup destruction and 20 hide/show cycles |
| Off switch | All sources released, pending retirements drained, zero NPU dispatches during off interval |
| Re-enable, clean exit, crash, invalid backend | Original desktop retained/restored; checks pass |
| Deliberately stalled owned UI thread | Supervisor kills its own child; output removed in 2.156 seconds |
| Normal supervised lifetime | Passes; no watchdog kill |
| Span provider | CPU, GPU and NPU pass disjoint spans crossing a dispatch boundary; untouched gaps checked |

The screen oracle reads the full output, excluding a 127×127 cursor neighborhood.
Delta has a persistent activation watermark. A two-color calibration models 6,763
OS-overlay pixels for this comparison only; it leaves the overlay visible. The
raw unmodeled screen error is 105. This is screen-readback evidence, not optical
scanout measurement or proof that the NPU owns OS overlays.

The original-image comparison uses the same retained-overlay model and a static
pre-capture snapshot. Differences cluster around window shadows and the taskbar.
Earlier per-pixel diagnostics found the taskbar capture nearly neutral gray with
alpha 250 where the original output was colored by the wallpaper. This establishes
that plain source-over of those captures is insufficient; it does not establish
an exact replacement for Windows' backdrop algorithm. Earlier sampled checks
missed much of this difference, so the final oracle compares the full image.

A successful component report explicitly says `full_desktop_offload: false`.
Client rendering, capture and transfer work remain. Exploratory traces also had
a test-harness console-window artifact; they are not used as a net GPU/Tahoma
benefit qualification. No broad performance conclusion is inferred from the
functional fixture's frame counter.

Destroyed capture sources initially caused synchronous `Close` to hang. The final
host retires capture interfaces on a separate initialized thread, recognizes
already-closed sources, bounds retirement time, and uses an external watchdog.
A resize fix drops incomplete frames left over from the old capture-pool extent.
The final stress sequence passed after these changes. Historical failure reports
are retained as exploratory evidence; they do not qualify the final source.

## Shared Windows allocations

Final build: `wddm-allocation-v7`; final runs `*-10*.jsonl`.

| Allocation owner | Extent | Changing frames | Worker backend | CPU and GPU readback error |
|---|---:|---:|---|---:|
| NPU | 64×64 | 32 | NPU | ≤1 byte |
| GPU | 64×64 | 32 | NPU | ≤1 byte |
| NPU | 1280×720 | 32 | NPU | ≤1 byte |
| NPU | 1920×1080 | 32 | NPU | ≤1 byte |
| NPU | 2880×1800 | 32 | NPU | ≤1 byte |

The producer creates standard `ExistingSection` VidMm allocations using the actual
adapter LUIDs, exports NT handles, and checks opening them on the other adapter.
An independently launched worker opens/maps/imports them and performs the NPU
blend. Parent/child events transfer ownership around synchronous inference; GPU
fences complete the validation copy before the next update. Every frame changes
both input surfaces and poisons output first to detect stale results.

The output section is also opened through D3D12's **diagnostic heap** API and used
as a cross-adapter buffer. GPU readback validates that it consumed the changed
NPU result without a CPU upload of those output pixels. This alias is a separate
GPU resource over common pages; it is not an ordinary D3D11 shared texture from
our graphics-driver laboratory. Internal vendor-driver copies are not measured.
CPU fixture writes, host waits and GPU validation copies remain.

Retained failed controls explain the implementation choice:

- ExistingSysMem allocation/import succeeds within one process (`npu-01`, `gpu-01`).
- An alias resource without `ALLOW_CROSS_ADAPTER` fails placement (`*-03`).
- Cross-process ExistingSysMem import fails with a Level Zero out-of-device-memory
  report (`*-06`). Explicit KMT opening in the child fails `0xc01e0100` (`*-07`).
- ExistingSection permits cross-process opening/import and correct NPU/GPU results
  (`*-08` exploratory, `*-10*` final source).

These failures do not establish general memory exhaustion, or that every possible
ExistingSysMem use is unsupported. They describe these exact tested routes.

## Remaining acceptance gap

The graphics-driver laboratory still rejects shared Direct3D textures and supports
one exact shader pair. The memory experiment supplies a real allocation primitive;
it does not connect those resources to graphics resource metadata, runtime
callbacks, a registered render adapter or DWM's actual shader/state stream.
Windows selection, full original-desktop fidelity, arbitrary application support,
retirement of existing GPU work and a repeatable Tahoma improvement remain open.

Reproduction and contracts: [capture host](../../../../native/windows/capture/README.md),
[shared allocation probe](../../../../native/windows/wddm-memory.md),
[Windows integration assessment](../../../research/windows-compositor.md).
