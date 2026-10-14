# Point-copy qualification and observed physical desktop content

Live v13 completes six minutes with 601 acknowledged NPU operations and 17
indirect frames. The physical presenter submits 13 matching frames. Windows
reports displayed frames, and a separate screen reader observes both controlled
red and green patches under the owned presenter window. Restoration passes.
Full desktop offload remains unfinished: responsiveness, primary-desktop input,
application coverage and Tahoma benefit are still unqualified.

## Point-copy specialization

The existing conservative coordinate proof now admits point samples whose entire
FP32 coordinate-error interval selects the same integer texel. A strict 0.49-texel
bound leaves margin at the selection boundary. Linear samples retain their
existing byte-error bound. The NPU performs extraction, channel exchange, alpha
replacement and merging; unsupported draws retain the general renderer, and
provider errors propagate. The general sampler is prepared only when needed.

The standalone CPU suite and two NPU runs each pass 573,456,384 component checks,
2,304 inference calls and seven rejected cases. Coverage includes clipped and
full rectangles, both channel orders, checkerboards, random data and quarter-texel
point offsets. Eight integrated sampler suites pass. A separate transition test
passes 120 draws and 1,474,560 checks on CPU and twice on NPU, alternating eligible
copies and general modulation with one executor.

Source v25 combines this specialization with v21's retained-model policy. Its
three full replay streams complete:

| Recorded requests | Available reference outputs | Exact matches | Unreferenced outputs |
| ---: | ---: | ---: | ---: |
| 117 | 116 | 116 | 1 |
| 256 | 255 | 255 | 1 |
| 582 | 581 | 581 | 1 |

The first two streams originally ended in failures. The third comes from the
successful six-minute native v11b recording, which was stopped with one recorded
operation still in flight. Replays establish consistency with recorded outputs;
they do not provide an independent CPU oracle for every recorded pixel.

The candidate passes eight operations through registered Direct3D before live
admission. Its native run has no provider stderr, and both renderer and display
cleanup pass. Source-frame intervals during physical presentation range from
0.236 to 53.313 seconds. The recorded workload and diagnostic overhead preclude
claiming a desktop speedup from the differing frame/operation totals.

## Texture reuse remains excluded

Source v23 combines the earlier scoped texture-upload reuse with retention. Its
CPU/NPU scope checks and 117-request replay pass. The first 256-request replay
matches 180 outputs and then loses the NPU device on a 384 × 64 clear. The unchanged
v21 control passes, and a traced repeat of v23 also passes. This is intermittent;
the evidence does not establish that texture reuse caused the failure. It remains
excluded from the live candidate.

The two passing traced controls each peak at 526 graph and 475 fence
create-minus-destroy calls, with one context, one queue and no warning/error
events. Event loss has not been independently checked. These results further
weaken a simple fixed-resource-count explanation. Source v22's missing
`d3dcompiler` link failure is preserved; v23 corrects it. Source v24 combines point
copy and texture reuse and builds, but is not admitted to hardware rendering.

## Physical presentation evidence

Presenter v10 adds direct DXGI statistics without changing rendering or copying.
Of 406 observations, 405 return successful standard frame statistics. The largest
reported displayed count and submitted count are both 13. Successful media
queries report NONE in 99 samples, COMPOSED in 260 and OVERLAY in 47. These are
observations over time, not counts of distinct frames. Presentation mode changes;
we have not established a fixed mode or measured residual GPU work.

Microsoft exposes displayed-frame timing separately from present-call acceptance;
media statistics additionally report a composition mode. The media mode's API
meaning does not certify that all GPU rendering has stopped.
[Frame statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/ns-dxgi-dxgi_frame_statistics),
[media statistics](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/ns-dxgi1_3-dxgi_frame_statistics_media),
[presentation-mode values](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/ne-dxgi1_3-dxgi_frame_presentation_mode).

An independent interactive observer reads two predetermined positions inside the
controlled patch with a screen DC and `GetPixel`. It verifies that the owned
presenter's window occupies each position and records only known-color matches,
not screenshots or arbitrary screen colors. Across 88 samples it records 107 red
and 68 green point matches, with one initially unknown point. Its child exits
successfully and its scheduled task is removed.

Screen reads sometimes block: the largest sampling interval is 18.896 seconds.
Timestamps precede read completion, so they cannot measure display latency or
identify a particular source frame. This is evidence of controlled content in
Windows' physical-desktop image, not an optical observation of the panel.

## Next integration boundary

Make the NPU-rendered logical desktop primary and place the physical presenter on
the other logical display. Ordinary Windows input can then target the primary
desktop directly; cursor confinement, window placement and exact restoration need
qualification. Microsoft defines the source at (0, 0) as primary, and IddCx uses a
software cursor by default. Neither document proves our implementation works.
[Desktop layout](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/desktop-layout),
[IddCx cursor behavior](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/iddcx/nf-iddcx-iddcxmonitorsetuphardwarecursor).

## Reproduction and integrity

The v22 patch applies to v21; v23 corrects v22; v24 adds point copy to v23. The
actually admitted v25 patch applies directly to v21 and excludes texture reuse.
Presenter v10 applies to v9. Standalone source and preparation scripts are under
`sources`; immutable script copies and all failed/successful reports are under
`raw`. The existing signed v15 driver package is unchanged.

All 226 collected artifacts, totaling 3,915,344 bytes, match hashes computed on
Delta. [raw-manifest.json](raw-manifest.json) maps original and archived paths.
Pixel recordings, model caches, binaries, ETL and signing keys are excluded.
[report.json](report.json) records evidence assertions. Runtime defaults remain
unchanged and the repository remains private.
