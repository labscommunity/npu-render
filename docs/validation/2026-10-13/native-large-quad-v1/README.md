# First native Windows desktop frame through the NPU renderer

Native trial v7 produces and reads back a **1024×768 desktop frame** from the
NPU-backed indirect display. Actual DWM completes 190 NPU operations with 190
delivery acknowledgments, the indirect driver confirms the requested render
adapter, and frame readback succeeds. The test removes its devices/packages,
restores exact process permissions, and preserves running memory integrity.

This closes the first native-frame milestone. It does **not** establish physical
desktop migration, input integration, sustained frame delivery, interactive
performance, removal of application GPU work, or a Tahoma benefit. The test does
not request a display topology change or disable the physical output. Automatic
Windows display routing during monitor arrival is not qualified as a primary
desktop switch.

## What changed

The preceding [trial](../native-large-triangle-v1/) rejects a quad-derivative
shader using a 2880×1800 source. Its isolated CPU and NPU replays already pass.
ABI 19 extends normalized resource-table textures to the qualified sampler's
larger capacity, retaining the 32 MiB aggregate packet ceiling and the existing
buffer/typed-load limits. Sampler and blend payloads retain their inherited ABI
identity. The worker round trip matches all 557,056 CPU reference bytes for
138,720 covered samples, and rejects twelve malformed/legacy packet cases.

Native-routing-v10's build initially fails because the new header omits
`<algorithm>`. Native-routing-v10b adds the include and passes the clean build,
packet/admission contracts, NPU worker round trip, component checks, and ordinary
registered Direct3D tests. The archive retains that failed build and both patches.
The signed renderer package is version 0.0.1308.0; the kernel binary is unchanged.

## Timeout and successful repeat

Trial v6 completes 86 NPU operations and reaches its 150-second observation
deadline while another shader is pending. The compositor remains alive through
the observation period. No frame is produced; cleanup succeeds.

Trial v7 repeats the same rendering binaries with existing model caches and a
210-second observation window. Its worker lifetime is 235 seconds, admission
remains 240 seconds, and the independent recovery task remains armed. It passes
the pending shader and receives frame sequence 1. Bootstrap plus its immediate
cleanup takes about **184 seconds**; the full installation/test/recovery
transaction takes about **206 seconds**. This is a first-frame experiment, not
a measured steady-state frame rate. Cached models do not remove the substantial
drawing and transfer costs established by earlier measurements.

## Frame evidence

The NRFRAME1 packet contains 3,145,728 BGRA pixel bytes from the pinned indirect
host PID 5352. Its complete packet SHA-256 is
`0fd870c3d7a53e3f3cddfd8e7ba51047b1ece235148e514b7cb49a937075bc6a`;
the pixel payload SHA-256 is
`4febd411edec523aa79202d5c7c318ebf75194e90a5dc8c5cb68468e4abbeff8`.
The downloaded packet matches the remote hash. A local preview shows desktop
wallpaper, icons, taskbar and a notification, with letterboxing. Visual inspection
is not an exact full-desktop reference comparison. Pixels and the preview are
excluded from Git.

The [report](report.json), [manifest](raw-manifest.json), controller sources and
patches preserve 130 remotely hash-verified text artifacts. These remain isolated
native candidates, with the default runtime unchanged. Physical presentation,
topology, input, sustained correctness, performance and Tahoma resource relief
remain outstanding.
