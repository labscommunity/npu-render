# Full-resolution draw and native display separation

Native trial v11 completes three different desktop frames, survives an extended
display layout, restores its original clone layout exactly, and removes both
diagnostic drivers. The final worker log contains **457 NPU operations and 457
delivery acknowledgments**. The controller observed 456 before removal; one more
operation settled during teardown. Memory integrity remained enabled and process
permissions were restored exactly.

The first frame arrived after **69.5 seconds**, while the displays were extended.
The next two arrived **93.7 and 115.4 seconds later**, after clone restoration.
This establishes a native frame during display separation and survival across
restoration. It does not establish sustained extended-desktop rendering, controlled
application changes, physical presentation, interaction, usable latency, or a
Tahoma benefit. Full desktop offload remains unfinished.

## Failures and fixes

ABI 20 adds a bounded large clear, preserving the old clear contract. CPU and NPU
each pass 24 cases covering 702,595,072 exact component checks. The 2880×1800
worker round trip verifies all 20,736,000 output bytes. This addresses a known
capacity gap; the earlier v9 clear failure lacked dimensions and is not proven to
have the same cause.

Trial v10 then fails on a captured 2880×1800 affine textured draw. Its 2912×1824
source and full destination background exceed the 32 MiB request limit. The new
exception trace identifies the packet rejection. The corrected bootstrap reports
failure even after a first frame, and v10 removes its devices successfully.

ABI 21 permits this large affine target/source combination in a bounded 64 MiB
request. Legacy packet bodies retain 32 MiB limits; rendered targets retain the
8,388,576-pixel reply bound. Sampling, modulation and coverage execute on the
selected provider. Full-size affine targets use batches of at most 16,384 pixels;
the other triangle schedulers retain their existing limits.

The first v12 CPU replay catches an unchanged 8,192-pixel scheduler guard.
Version v12b adds an explicit large-affine scheduling option and regression
checks. Both failures and their fixes are preserved as sequential patches.

## Qualification and limits

The captured full-resolution draw matches **all 20,736,000 CPU reference bytes**
on the NPU. Its C++ client/worker round trip also matches exactly, with a
41,984,152-byte request, a 20,736,128-byte reply, and one acknowledged operation.
It uses 360 raster tiles and 362 provider requests. The offline NPU draw takes
about **16.25 seconds**, excluding approximately 2.87 seconds of model setup.
These are isolated draw timings, not desktop frame rates. Declared input tensor
bytes are not measurements of host/device memory traffic.

The candidate passes packet/admission contracts, ten NPU component operations,
package staging/recovery, and ordinary registered Direct3D execution. Native
package version 0.0.1310.0 uses the unchanged kernel binary. Its controller and
the separate topology coordinator have distinct scopes: the controller's
`display_topology_requested: false` does not mean the coordinator left topology
unchanged.

The [report](report.json), [manifest](raw-manifest.json), patches and controllers
preserve 251 remotely hash-verified text artifacts. Apply patches v11, v11b,
v12 and v12b in order on the preceding
[native-routing-v10b candidate](../../2026-10-13/native-large-quad-v1/).
Captured pixels, models, caches and binaries are excluded. The default runtime
is unchanged; these remain explicitly bounded research candidates.
