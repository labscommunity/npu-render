# Windows shared allocation experiment

`nr_wddm_allocation_probe` exercises real Windows video-memory-manager resources
on the exact GPU/NPU LUIDs reported by OpenVINO. It registers no system driver and
does not provide a selectable DWM adapter.

A producer creates page-aligned, section-backed standard allocations through
`D3DKMTCreateAllocation2`, with cross-adapter and NT sharing enabled. Another
adapter opens their real resource handles. An explicitly launched worker process
receives only whitelisted resource/event/log handles, opens the allocations, and
imports them into OpenVINO's NPU remote tensors. It signals completion only after
synchronous NPU inference finishes. The parent validates changing source-over
results against an independent integer oracle, through both the CPU mapping and
a GPU readback of the result.

The GPU view uses `ID3D12Device3::OpenExistingHeapFromFileMapping`, a documented
**diagnostic heap** API. It aliases the section's pages into a graphics resource;
it is not `ID3D11Device::OpenSharedResource` of our experimental graphics driver.
The separate KMT handle-open test and this GPU consumption test establish distinct
parts of the memory boundary. No NPU output pixels are uploaded by the CPU to the
GPU, but CPU fixture writes, host synchronization, GPU validation copies, and
possible internal vendor-driver transfers remain. This is not a general zero-copy
or asynchronous-fence claim.

## Build and run

```powershell
uv sync --frozen --group native
uv run --frozen --group native python tools/build_native.py
$env:PATH=(uv run python -c "import openvino,pathlib; print(pathlib.Path(openvino.__file__).parent/'libs')")+';'+$env:PATH
build/native/Release/nr_wddm_allocation_probe.exe NPU 1920 1080 32
build/native/Release/nr_wddm_allocation_probe.exe GPU 64 64 32
```

The first argument chooses the allocation-owning driver. Computation always runs
on the NPU and GPU consumption always uses the exact graphics adapter. Width must
be divisible by four, dimensions are at most 4096, and frame count is 1–1000. The
default is 64×64 for 32 frames. The last JSON line is the aggregate result; preceding
lines retain operation status and worker diagnostics. Missing devices, failed
imports, stale pixels and worker deadlines return failure without another backend.
This hardware experiment is not a CPU CI test.

## What changed the result

Existing-system-memory allocations worked within one process. In a second process,
resource metadata queries succeeded but opening the resource failed with
`0xc01e0100`; OpenVINO import reported insufficient device memory. Replacing the
private virtual allocation with an **ExistingSection** standard allocation allowed
the child to open/map/import the resource. The GPU alias also needs the
`ALLOW_CROSS_ADAPTER` resource flag required by its shared cross-adapter heap.
These are observations on Delta's installed stack, not a universal claim about all
Windows devices or allocation modes.

## Required next integration

The current D3D11 laboratory still has host-backed private resources and rejects
shared textures. It must acquire proper graphics resource identities, preserve
format/pitch/ownership metadata, and implement shared resource callbacks before
this memory primitive can serve that integration. DWM also needs a registered
compatible render adapter, its real shader/state stream, presentation and recovery.
The Intel NPU's successful KMT allocation calls do not change the fact that its
installed driver rejects the tested Direct3D graphics device-creation routes.

References: [standard allocation contracts](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtcreateallocation),
[cross-adapter resource requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/using-cross-adapter-resources-in-a-hybrid-system),
[diagnostic heap from a file mapping](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12device3-openexistingheapfromfilemapping).
