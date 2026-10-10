// Actual UMD callbacks with owned runtime shims. This is not a live adapter test.
#include "driver.cpp"
#include "blend_reference.hpp"
#include <cstdio>
#include <vector>
#include <map>
#include <cmath>
#define TEST(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)
namespace {
// Two complete 11-level textures plus transfer allocations in the mip tests.
std::array<std::vector<unsigned char>, 64> memory;
std::array<bool, 64> locked{};
std::array<UINT, 64> references{};
std::map<HANDLE, UINT> resource_handles;
nr::kernel_contract::FillSurface commands[2]{};
D3DDDI_ALLOCATIONLIST lists[2][2]{};
UINT current_buffer = 0, renders = 0, destroys = 0, released = 0;
bool contract_error = false, allocation_failure = false, render_failure = false;
bool lock_failure = false, lock_busy=false, last_lock_read_only=false, last_lock_write_only=false, last_lock_do_not_wait=false;
bool emulate_copies = false;
std::vector<std::pair<UINT, UINT>> pending_copies;
HRESULT last_error = S_OK;
unsigned adapter_queries = 0;
HRESULT APIENTRY query_adapter_cb(HANDLE, const D3DDDICB_QUERYADAPTERINFO *args) {
    ++adapter_queries;
    if (!args || args->PrivateDriverDataSize != sizeof(nr::kernel_contract::AdapterInfo)) return E_INVALIDARG;
    const nr::kernel_contract::AdapterInfo info{};
    std::memcpy(args->pPrivateDriverData, &info, sizeof(info));
    return S_OK;
}
HANDLE runtime_handle = reinterpret_cast<HANDLE>(uintptr_t(0x1234));
HANDLE context_handle = reinterpret_cast<HANDLE>(uintptr_t(0x5678));
void verify(bool v) { if (!v) contract_error = true; }
void APIENTRY set_error(D3D10DDI_HRTCORELAYER, HRESULT status) { last_error = status; }
HRESULT APIENTRY create_context_cb(HANDLE rt, D3DDDICB_CREATECONTEXT *a) {
    verify(rt == runtime_handle && a->EngineAffinity == 1 && !a->NodeOrdinal && !a->Flags.Value);
    a->hContext = context_handle;
    a->pCommandBuffer = &commands[current_buffer]; a->CommandBufferSize = sizeof(commands[0]);
    a->pAllocationList = lists[current_buffer]; a->AllocationListSize = 2;
    return S_OK;
}
HRESULT APIENTRY destroy_context_cb(HANDLE rt, const D3DDDICB_DESTROYCONTEXT *a) {
    verify(rt == runtime_handle && a->hContext == context_handle);
    for (const auto &m : memory) verify(m.empty());
    ++destroys;
    return S_OK;
}
HRESULT APIENTRY allocate_cb(HANDLE rt, D3DDDICB_ALLOCATE *a) {
    verify(rt == runtime_handle && a->NumAllocations == 1);
    verify(a->hResource ? a->PrivateDriverDataSize == sizeof(nr::kernel_contract::Surface) && a->pPrivateDriverData
                       : !a->PrivateDriverDataSize);
    if (allocation_failure) return E_OUTOFMEMORY;
    const auto &s = *static_cast<const nr::kernel_contract::Surface *>(a->pAllocationInfo->pPrivateDriverData);
    verify(nr::kernel_contract::valid(s));
    for (UINT i = 1; i < memory.size(); ++i) {
        if (memory[i].empty()) {
            memory[i].resize(size_t(s.allocation_bytes), 0x35); references[i] = 1;
            a->pAllocationInfo->hAllocation = i;
            if (a->hResource) { resource_handles[a->hResource] = i; a->hKMResource = 77; }
            return S_OK;
        }
    }
    return E_OUTOFMEMORY;
}
HRESULT APIENTRY deallocate_cb(HANDLE rt, const D3DDDICB_DEALLOCATE *a) {
    verify(rt == runtime_handle);
    UINT i = 0;
    if (a->hResource) {
        verify(!a->NumAllocations && !a->HandleList);
        auto found = resource_handles.find(a->hResource);
        if (found == resource_handles.end()) return E_INVALIDARG;
        i = found->second; resource_handles.erase(found);
    } else {
        verify(a->NumAllocations == 1 && a->HandleList);
        i = *a->HandleList;
    }
    if (i >= memory.size() || memory[i].empty() || locked[i]) return E_INVALIDARG;
    for (const auto &copy : pending_copies) verify(copy.first != i && copy.second != i);
    if (!--references[i]) { memory[i].clear(); ++released; }
    return S_OK;
}
HRESULT APIENTRY lock_cb(HANDLE rt, D3DDDICB_LOCK *a) {
    verify(rt == runtime_handle && a->Flags.LockEntire && a->Flags.DonotEvict && !a->Flags.Discard);
    last_lock_read_only=a->Flags.ReadOnly!=0;last_lock_write_only=a->Flags.WriteOnly!=0;
    last_lock_do_not_wait=a->Flags.DonotWait!=0;
    verify(!(last_lock_read_only && last_lock_write_only));
    if(lock_busy && a->Flags.DonotWait)return D3DDDIERR_WASSTILLDRAWING;
    if (lock_failure && !a->Flags.ReadOnly) return E_ACCESSDENIED;
    const auto i = a->hAllocation;
    for (const auto &resource : resource_handles) if (resource.second == i) return E_ACCESSDENIED;
    if (i >= memory.size() || memory[i].empty() || locked[i]) return E_INVALIDARG;
    // Explicit test-only executor. Deferred until a blocking lock, so tests
    // detect upload storage that is freed or modified before submission retires.
    for (const auto &copy : pending_copies) {
        verify(memory[copy.first].size() == memory[copy.second].size());
        memory[copy.second] = memory[copy.first];
    }
    pending_copies.clear();
    locked[i] = true; a->pData = memory[i].data();
    return S_OK;
}
HRESULT APIENTRY unlock_cb(HANDLE rt, const D3DDDICB_UNLOCK *a) {
    verify(rt == runtime_handle && a->NumAllocations == 1 && a->phAllocations);
    const auto i = *a->phAllocations;
    if (i >= memory.size() || !locked[i]) return E_INVALIDARG;
    locked[i] = false;
    return S_OK;
}
HRESULT APIENTRY render_cb(HANDLE rt, D3DDDICB_RENDER *a) {
    verify(rt == runtime_handle && a->hContext == context_handle && a->CommandLength == sizeof(commands[0]) &&
           !a->NumPatchLocations && !a->CommandOffset);
    verify(commands[current_buffer].signature == nr::kernel_contract::magic);
    if (commands[current_buffer].operation == 2) {
        verify(a->NumAllocations == 2 && !lists[current_buffer][0].WriteOperation && lists[current_buffer][1].WriteOperation);
        verify(lists[current_buffer][0].hAllocation != lists[current_buffer][1].hAllocation);
        verify(!commands[current_buffer].pattern && !commands[current_buffer].reserved);
    } else {
        verify(a->NumAllocations == 1 && commands[current_buffer].operation == 1 && commands[current_buffer].pattern == 0xff0000ff);
        verify(lists[current_buffer][0].hAllocation == 1 && lists[current_buffer][0].WriteOperation);
    }
    ++renders;
    if (render_failure) return E_ACCESSDENIED;
    if (emulate_copies && commands[current_buffer].operation == 2)
        pending_copies.emplace_back(lists[current_buffer][0].hAllocation, lists[current_buffer][1].hAllocation);
    current_buffer ^= 1;
    a->pNewCommandBuffer = &commands[current_buffer]; a->NewCommandBufferSize = sizeof(commands[0]);
    a->pNewAllocationList = lists[current_buffer]; a->NewAllocationListSize = 2;
    return S_OK;
}
}
#include "upload_test.hpp"
#include "staging_map_test.hpp"
#include "draw_test.hpp"
#include "indexed_draw_test.hpp"
#include "present_test.hpp"
#include "rotate_test.hpp"
#include "copy_region_test.hpp"
#include "mip_test.hpp"
#include "geometry_test.hpp"
#include "buffer_test.hpp"
int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // Preserve the last completed check if a callback crashes.
    const char *backend = argc >= 2 ? argv[1] : "CPU";
    const bool desktop_size = argc == 3 && !std::strcmp(argv[2], "--desktop-size");
    TEST(argc <= 2 || desktop_size);
    TEST(OpenAdapter10_2(nullptr) == E_INVALIDARG);
    if (!nr_fill_guest()) {
        D3DDDI_ADAPTERCALLBACKS callbacks{};
        callbacks.pfnQueryAdapterInfoCb = query_adapter_cb;
        D3D10_2DDI_ADAPTERFUNCS funcs{};
        D3D10DDIARG_OPENADAPTER args{};
        args.pAdapterCallbacks = &callbacks; args.pAdapterFuncs_2 = &funcs;
        TEST(OpenAdapter10_2(&args) == DXGI_ERROR_UNSUPPORTED);
        TEST(!adapter_queries && !args.hAdapter.pDrvPrivate && !funcs.pfnCreateDevice);
    }
    D3DDDI_DEVICECALLBACKS kt{};
    kt.pfnCreateContextCb = create_context_cb; kt.pfnDestroyContextCb = destroy_context_cb;
    kt.pfnAllocateCb = allocate_cb; kt.pfnDeallocateCb = deallocate_cb;
    kt.pfnLockCb = lock_cb; kt.pfnUnlockCb = unlock_cb; kt.pfnRenderCb = render_cb;
    D3D10DDI_CORELAYER_DEVICECALLBACKS um{}; um.pfnSetErrorCb = set_error;
    D3D10DDI_DEVICEFUNCS functions{};
    DXGI_DDI_BASE_FUNCTIONS dxgi{};
    alignas(Device) unsigned char storage[sizeof(Device)]{};
    D3D10DDIARG_CREATEDEVICE create{};
    create.hDrvDevice.pDrvPrivate = storage; create.hRTDevice.handle = runtime_handle;
    create.pDeviceFuncs = &functions; create.pUMCallbacks = &um; create.pKTCallbacks = &kt;
    create.DXGIBaseDDI.pDXGIDDIBaseFunctions = &dxgi;
    create.Interface = D3D10_0_DDI_INTERFACE_VERSION;
    TEST(create_device({}, &create) == DXGI_STATUS_NO_REDIRECTION);
    const auto h = create.hDrvDevice;
    TEST(counters.live_contexts == 1);
    D3D10_DDI_BLEND_DESC blend_desc{};
    blend_desc.RenderTargetWriteMask[0] = 5;
    D3D10_DDI_BLEND_DESC blend_storage{};
    D3D10DDI_HBLENDSTATE blend_handle{}; blend_handle.pDrvPrivate = &blend_storage;
    TEST(functions.pfnCalcPrivateBlendStateSize(h, &blend_desc) == sizeof(blend_desc));
    functions.pfnCreateBlendState(h, &blend_desc, blend_handle, {});
    FLOAT factors[4] = {.1f, .2f, .3f, .4f};
    functions.pfnSetBlendState(h, blend_handle, factors, 0x7f);
    TEST(last_error == S_OK && device(h).blend.RenderTargetWriteMask[0] == 5 && device(h).sample_mask == 0x7f);
    TEST(device(h).blend_factor[2] == factors[2]);
    functions.pfnDestroyBlendState(h, blend_handle);
    functions.pfnSetBlendState(h, {}, nullptr, ~0u);
    functions.pfnSetDepthStencilState(h, {}, 3);
    functions.pfnSetRasterizerState(h, {});
    functions.pfnSetRenderTargets(h, nullptr, 0, 8, {});
    TEST(last_error == S_OK && device(h).stencil_ref == 3 && device(h).depth.DepthEnable &&
         device(h).raster.DepthClipEnable && !device(h).bound_target);
    D3D10_DDI_SAMPLER_DESC sampler_desc{}, sampler_storage{};
    sampler_desc.MaxAnisotropy = 4;
    D3D10DDI_HSAMPLER sampler_handle{}; sampler_handle.pDrvPrivate = &sampler_storage;
    functions.pfnCreateSampler(h, &sampler_desc, sampler_handle, {});
    functions.pfnVsSetSamplers(h, 15, 1, &sampler_handle);
    functions.pfnGsSetSamplers(h, 4, 1, &sampler_handle);
    functions.pfnPsSetSamplers(h, 0, 1, &sampler_handle);
    functions.pfnDestroySampler(h, sampler_handle);
    sampler_storage.MaxAnisotropy = 0; // Bindings own descriptors, not private-storage pointers.
    TEST(last_error == S_OK && device(h).samplers[0][15].desc.MaxAnisotropy == 4 &&
         device(h).samplers[1][4].desc.MaxAnisotropy == 4 && device(h).samplers[2][0].bound);
    functions.pfnVsSetSamplers(h, 15, 2, &sampler_handle);
    TEST(last_error == E_INVALIDARG && device(h).samplers[0][15].desc.MaxAnisotropy == 4);
    last_error = S_OK;
    D3D10DDI_HSAMPLER empty_samplers[16]{};
    functions.pfnVsSetSamplers(h, 0, 16, empty_samplers);
    TEST(last_error == S_OK && !device(h).samplers[0][15].bound && device(h).samplers[1][4].bound);
    alignas(Resource) unsigned char target_storage[sizeof(Resource)]{}, staging_storage[sizeof(Resource)]{}, failed_storage[sizeof(Resource)]{};
    D3D10DDI_HRESOURCE target{}, staging{}, failed{};
    target.pDrvPrivate = target_storage; staging.pDrvPrivate = staging_storage; failed.pDrvPrivate = failed_storage;
    D3D10DDI_MIPINFO mip{};
    mip.TexelWidth = 64; mip.TexelHeight = 48; mip.TexelDepth = 1;
    D3D10DDIARG_CREATERESOURCE args{};
    args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    args.MipLevels = args.ArraySize = 1; args.pMipInfoList = &mip;
    args.Format = DXGI_FORMAT_R8G8B8A8_UNORM; args.SampleDesc.Count = 1;
    args.Usage = D3D10_DDI_USAGE_DEFAULT; args.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
    allocation_failure = true;
    functions.pfnCreateResource(h, &args, failed, {});
    TEST(last_error == E_OUTOFMEMORY && counters.live_allocations == 0);
    functions.pfnDestroyResource(h, failed);
    allocation_failure = false; last_error = S_OK;
    functions.pfnCreateResource(h, &args, target, {});
    TEST(last_error == S_OK && counters.live_allocations == 1);
    args.Usage = D3D10_DDI_USAGE_STAGING; args.BindFlags = 0; args.MapFlags = D3D10_DDI_CPU_ACCESS_READ;
    functions.pfnCreateResource(h, &args, staging, {});
    TEST(last_error == S_OK && counters.live_allocations == 2);
    alignas(View) unsigned char view_storage[sizeof(View)]{};
    D3D10DDI_HRENDERTARGETVIEW view{}; view.pDrvPrivate = view_storage;
    D3D10DDIARG_CREATERENDERTARGETVIEW view_args{};
    view_args.hDrvResource = target; view_args.ResourceDimension = D3D10DDIRESOURCE_TEXTURE2D;
    view_args.Format = DXGI_FORMAT_R8G8B8A8_UNORM; view_args.Tex2D.ArraySize = 1;
    functions.pfnCreateRenderTargetView(h, &view_args, view, {});
    functions.pfnSetRenderTargets(h, &view, 1, 0, {});
    TEST(last_error == S_OK && device(h).bound_target == resource(target).allocation);
    D3D10DDI_HRENDERTARGETVIEW invalid_views[2] = {view, view};
    functions.pfnSetRenderTargets(h, invalid_views, 2, 0, {});
    TEST(last_error == E_INVALIDARG && device(h).bound_target == resource(target).allocation);
    last_error = S_OK;
    FLOAT color[4] = {1, 0, 0, 1};
    functions.pfnClearRenderTargetView(h, view, color);
    functions.pfnClearRenderTargetView(h, view, color);
    TEST(last_error == S_OK && renders == 2 && counters.fills == 2 && !contract_error);
    TEST(memory[1][0] == 0x35); // The UMD did not silently replace submission with a CPU clear.
    lock_failure = true;
    functions.pfnResourceCopy(h, staging, target);
    TEST(last_error == E_ACCESSDENIED && !locked[1] && !locked[2]);
    lock_failure = false; last_error = S_OK;
    memory[1][0] = 0x94;
    functions.pfnResourceCopy(h, staging, target);
    TEST(last_error == S_OK && memory[2][0] == 0x94 && !locked[1] && !locked[2]);
    D3D10DDI_MAPPED_SUBRESOURCE mapped{};
    functions.pfnStagingResourceMap(h, staging, 0, D3D10_DDI_MAP_READ, 0, &mapped);
    TEST(last_error == S_OK && mapped.pData == memory[2].data() && locked[2]);
    functions.pfnStagingResourceUnmap(h, staging, 0);
    TEST(!locked[2]);
    functions.pfnDestroyRenderTargetView(h, view);
    functions.pfnDestroyResource(h, target); functions.pfnDestroyResource(h, staging);
    // Runtime-owned identities retain a shared allocation after its creator
    // closes, and release the import through its own runtime resource handle.
    functions.pfnSetRenderTargets(h, nullptr, 0, 8, {});
    alignas(Resource) unsigned char shared_storage[sizeof(Resource)]{}, imported_storage[sizeof(Resource)]{};
    D3D10DDI_HRESOURCE shared{}, imported{};
    shared.pDrvPrivate = shared_storage; imported.pDrvPrivate = imported_storage;
    D3D10DDI_HRTRESOURCE owner{}, borrower{};
    owner.handle = reinterpret_cast<HANDLE>(uintptr_t(0x9001)); borrower.handle = reinterpret_cast<HANDLE>(uintptr_t(0x9002));
    args.Usage = D3D10_DDI_USAGE_DEFAULT; args.BindFlags = D3D10_DDI_BIND_RENDER_TARGET;
    args.MapFlags = 0; args.MiscFlags = D3D10_DDI_RESOURCE_MISC_SHARED;
    last_error = S_OK;
    functions.pfnCreateResource(h, &args, shared, owner);
    TEST(last_error == S_OK && resource_handles.count(owner.handle) == 1);
    const auto shared_index = resource_handles.at(owner.handle);
    auto description = resource(shared).allocation->surface;
    D3DDDI_OPENALLOCATIONINFO opened{};
    opened.hAllocation = shared_index; opened.pPrivateDriverData = &description; opened.PrivateDriverDataSize = sizeof(description);
    D3D10DDIARG_OPENRESOURCE open{};
    open.NumAllocations = 1; open.pOpenAllocationInfo = &opened; open.hKMResource.handle = 77;
    open.pPrivateDriverData = &description; open.PrivateDriverDataSize = sizeof(description);
    resource_handles[borrower.handle] = shared_index; ++references[shared_index]; // OS opens before the UMD callback.
    functions.pfnOpenResource(h, &open, imported, borrower);
    TEST(last_error == S_OK && counters.live_allocations == 2);
    functions.pfnDestroyResource(h, shared);
    TEST(!resource_handles.count(owner.handle) && references[shared_index] == 1 && !memory[shared_index].empty());
    args.Usage = D3D10_DDI_USAGE_STAGING; args.BindFlags = 0;
    args.MapFlags = D3D10_DDI_CPU_ACCESS_READ; args.MiscFlags = 0;
    functions.pfnCreateResource(h, &args, staging, {});
    const auto staging_index = resource(staging).allocation->handle;
    memory[shared_index][0] = 0x94;
    functions.pfnResourceCopy(h, staging, imported);
    TEST(last_error == S_OK && renders == 3 && counters.submitted_copies == 1);
    TEST(memory[staging_index][0] == 0x35 && !locked[shared_index] && !locked[staging_index]);
    // Submission shims do not execute. A successful call must not secretly lock
    // and copy the shared allocation on the UMD's CPU thread.
    render_failure = true;
    functions.pfnResourceCopy(h, staging, imported);
    TEST(last_error == E_ACCESSDENIED && renders == 4 && counters.submitted_copies == 1);
    functions.pfnResourceCopy(h, staging, imported);
    TEST(last_error == E_INVALIDARG && renders == 4); // No reuse of failed callback buffers.
    functions.pfnDestroyResource(h, staging);
    functions.pfnDestroyResource(h, imported);
    TEST(resource_handles.empty() && memory[shared_index].empty());
    functions.pfnDestroyDevice(h);
    TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    TEST(released == 4 && destroys == 1);
    TEST(test_uploads(kt, um) == 0);
    TEST(test_staging_maps(kt, um) == 0);
    TEST(test_present(kt, um) == 0);
    TEST(test_rotation(kt, um) == 0);
    TEST(test_copy_regions(kt, um) == 0);
    TEST(test_mips(kt, um) == 0);
    TEST(test_buffers(kt, um) == 0);
    TEST(test_draws(kt, um, backend, desktop_size) == 0);
    TEST(test_indexed_draw(kt, um, backend) == 0);
    std::puts("registered lab allocation and submission contracts passed");
    return 0;
}
