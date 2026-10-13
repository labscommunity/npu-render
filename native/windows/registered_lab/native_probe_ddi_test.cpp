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

#include "clear_ddi_test.hpp"
#include "native_indexed_probe_test.hpp"
int main(int argc, char **argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    TEST(argc == 1 || (argc == 2 && !std::strcmp(argv[1], "--reject-unleased")));
    D3DDDI_ADAPTERCALLBACKS callbacks{}; callbacks.pfnQueryAdapterInfoCb = query_adapter_cb;
    D3D10_2DDI_ADAPTERFUNCS funcs{}; D3D10DDIARG_OPENADAPTER args{};
    args.pAdapterCallbacks = &callbacks; args.pAdapterFuncs_2 = &funcs;
    TEST(!nr_fill_configure("CPU") && !nr_fill_configure("GPU"));
    if (argc == 2) {
        TEST(!nr_fill_configure("NPU") && OpenAdapter10_2(&args) == DXGI_ERROR_UNSUPPORTED && !adapter_queries);
        std::printf("{\"unleased_native_adapter_rejected\":true,\"passed\":true,\"provider_requests\":0}\n"); return 0;
    }
    TEST(nr_fill_configure("NPU"));
    TEST(OpenAdapter10_2(&args) == S_OK && adapter_queries == 1 && funcs.pfnCreateDevice && funcs.pfnCloseAdapter);
    D3DDDI_DEVICECALLBACKS kt{};
    kt.pfnCreateContextCb = create_context_cb; kt.pfnDestroyContextCb = destroy_context_cb;
    kt.pfnAllocateCb = allocate_cb; kt.pfnDeallocateCb = deallocate_cb;
    kt.pfnLockCb = lock_cb; kt.pfnUnlockCb = unlock_cb; kt.pfnRenderCb = render_cb;
    D3D10DDI_CORELAYER_DEVICECALLBACKS um{}; um.pfnSetErrorCb = set_error;
    TEST(test_provider_clears(kt,um,"NPU") == 0);
    TEST(test_indexed_draw(kt,um,"NPU") == 0);
    TEST(counters.executions == 10 && counters.remote_npu_executions == 10 && !counters.cpu_executions && !counters.gpu_executions && !counters.npu_executions);
    TEST(!std::strcmp(counters.last_execution_device,"NPU (local indexed)"));
    TEST(!counters.live_allocations && !counters.live_contexts && !counters.cleanup_errors && !contract_error);
    TEST(funcs.pfnCloseAdapter(args.hAdapter) == S_OK);
    std::printf("{\"native_probe_ddi_component\":true,\"passed\":true,\"npu_local_executions\":10,\"clears\":8,\"draws\":2,\"cpu_executions\":0,\"gpu_executions\":0,\"allocation_callbacks_emulated\":true,\"registered_adapter_tested\":false,\"desktop_offloaded\":false}\n"); return 0;
}
