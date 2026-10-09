// Actual UMD callbacks with owned runtime shims. This is not a live adapter test.
#include "driver.cpp"
#include <cstdio>
#include <vector>
#define TEST(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)
namespace {
std::array<std::vector<unsigned char>, 8> memory;
std::array<bool, 8> locked{};
nr::kernel_contract::FillSurface commands[2]{};
D3DDDI_ALLOCATIONLIST lists[2]{};
UINT current_buffer = 0, renders = 0, destroys = 0, released = 0;
bool contract_error = false, allocation_failure = false, render_failure = false;
bool lock_failure = false;
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
    a->pAllocationList = &lists[current_buffer]; a->AllocationListSize = 1;
    return S_OK;
}
HRESULT APIENTRY destroy_context_cb(HANDLE rt, const D3DDDICB_DESTROYCONTEXT *a) {
    verify(rt == runtime_handle && a->hContext == context_handle);
    for (const auto &m : memory) verify(m.empty());
    ++destroys;
    return S_OK;
}
HRESULT APIENTRY allocate_cb(HANDLE rt, D3DDDICB_ALLOCATE *a) {
    verify(rt == runtime_handle && !a->hResource && !a->PrivateDriverDataSize && a->NumAllocations == 1);
    if (allocation_failure) return E_OUTOFMEMORY;
    const auto &s = *static_cast<const nr::kernel_contract::Surface *>(a->pAllocationInfo->pPrivateDriverData);
    verify(nr::kernel_contract::valid(s));
    for (UINT i = 1; i < memory.size(); ++i) {
        if (memory[i].empty()) { memory[i].resize(size_t(s.allocation_bytes), 0x35); a->pAllocationInfo->hAllocation = i; return S_OK; }
    }
    return E_OUTOFMEMORY;
}
HRESULT APIENTRY deallocate_cb(HANDLE rt, const D3DDDICB_DEALLOCATE *a) {
    verify(rt == runtime_handle && !a->hResource && a->NumAllocations == 1 && a->HandleList);
    const auto i = *a->HandleList;
    if (i >= memory.size() || memory[i].empty() || locked[i]) return E_INVALIDARG;
    memory[i].clear(); ++released;
    return S_OK;
}
HRESULT APIENTRY lock_cb(HANDLE rt, D3DDDICB_LOCK *a) {
    verify(rt == runtime_handle && a->Flags.LockEntire && a->Flags.DonotEvict && !a->Flags.Discard);
    if (lock_failure && a->Flags.WriteOnly) return E_ACCESSDENIED;
    const auto i = a->hAllocation;
    if (i >= memory.size() || memory[i].empty() || locked[i]) return E_INVALIDARG;
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
           a->NumAllocations == 1 && !a->NumPatchLocations && !a->CommandOffset);
    verify(commands[current_buffer].signature == nr::kernel_contract::magic && commands[current_buffer].pattern == 0xff0000ff);
    verify(lists[current_buffer].hAllocation == 1 && lists[current_buffer].WriteOperation);
    ++renders;
    if (render_failure) return E_ACCESSDENIED;
    current_buffer ^= 1;
    a->pNewCommandBuffer = &commands[current_buffer]; a->NewCommandBufferSize = sizeof(commands[0]);
    a->pNewAllocationList = &lists[current_buffer]; a->NewAllocationListSize = 1;
    return S_OK;
}
}
int main() {
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
    TEST(create_device({}, &create) == S_OK);
    const auto h = create.hDrvDevice;
    TEST(counters.live_contexts == 1);
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
    render_failure = true;
    functions.pfnClearRenderTargetView(h, view, color);
    TEST(last_error == E_ACCESSDENIED && renders == 3);
    functions.pfnClearRenderTargetView(h, view, color);
    TEST(last_error == E_INVALIDARG && renders == 3); // Failed submission poisoned old pointers.
    functions.pfnDestroyRenderTargetView(h, view);
    functions.pfnDestroyResource(h, target); functions.pfnDestroyResource(h, staging);
    functions.pfnDestroyDevice(h);
    TEST(!counters.live_contexts && !counters.live_allocations && !counters.cleanup_errors && !contract_error);
    TEST(released == 2 && destroys == 1);
    std::puts("registered lab allocation and submission contracts passed");
    return 0;
}
