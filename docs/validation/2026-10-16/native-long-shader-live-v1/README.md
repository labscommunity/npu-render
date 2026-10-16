# Long-shader worker: finite native desktop qualification

Live v44 passes on Delta with qualified worker v9: 1,112 NPU operation receipts,
1,112 delivery acknowledgments and 1,112 isolated child receipts. There is one
provider generation, no provider faults and no retries. The physical presenter
submits 51 frames, all matching their source pixels, with 33 distinct source
hashes. This is a finite changing-desktop test, not a frame-rate or sustained-use
qualification.

The repaired long-shader path executes four times with one shared import.
The worker also admits 432 reusable-program draws, 240 shared-geometry draws,
23 four-sample draws, five solid draws and two three-texture draws. The reusable
program and geometry paths each import their model once. The prior v43 timeout
and its original evidence remain preserved separately.

Controlled mouse and keyboard events reach their owned Windows stimulus. The
physical-output observer verifies the blue, green and red responses. Relative
pointer movement, confinement, two reacquisitions and cursor restoration pass.
The exact display topology and ordinary windows are restored; driver cleanup and
memory-integrity checks pass. These checks do not cover secure desktop or sleep/wake.

| Controlled event | Frame publication | Matching display statistics | Independent pixel read completed |
| --- | ---: | ---: | ---: |
| Mouse | 4,409.902 ms | 4,454.417 ms | 4,541.028 ms |
| Keyboard | 106.232 ms | See retained presentation report | 172.758 ms |

These are two events, not a percentile distribution or a matched comparison with
earlier runs. The observer reads the physical output through DXGI; it does not
measure panel photons. The initial 30 fps and p95 response ≤100 ms targets remain
unmet. The current worker's thirty-minute duration gate remains open.

The causal trace places 3,482.198 ms of the mouse delay inside serialized child
requests before publication, including compilation and host orchestration.
Another 927.704 ms is outside those child intervals before publication. Two shader
groups account for 2,207.444 ms: `595296ba…` contributes 1,114.665 ms across eight
requests and `c71638aa…` contributes 1,092.779 ms across two. These groups guide
the next rendering changes; the newly repaired long shader is not the remaining
dominant cost in this event.

The presenter still uses GPU copies and physical scanout. The independent
measurement observer alone records 152 GPU copy commands. Residual GPU time,
power and Tahoma performance have not been measured in a matched native-desktop
comparison. Automatic interactive application graphics remain a separate gate;
this run does not establish migration of browser, video or game rendering.

The archive pins 99 source/evidence artifacts by byte length and SHA-256. Private
pixel packets, compiled models and binaries are excluded. The executed presentation
analyzer v2 accepts the coordinator's omitted `--timing` argument by selecting the
retained causal report. Its actual source is archived; the preparation script
retains its earlier required-argument template. The live bootstrap's worker exit
after owned device removal is recorded separately from provider faults.

Full desktop offload and a Tahoma benefit remain unqualified.
