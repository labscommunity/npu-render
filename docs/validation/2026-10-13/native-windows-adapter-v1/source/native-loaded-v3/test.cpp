#define NOMINMAX
#include <windows.h>
#pragma warning(push)
#pragma warning(disable: 4201) // Microsoft DDI anonymous structures.
#include <d3d10umddi.h>
#pragma warning(pop)
#include "kernel_contract.hpp"
#include "contract.hpp"
#include <cstdio>
#include <cstddef>
#include <algorithm>
#include <vector>
#include <map>
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#define TEST(x) do { if (!(x)) { std::fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (false)
namespace {
// Two complete 11-level textures plus transfer allocations in the mip tests.
std::array<std::vector<unsigned char>, 64> memory;
std::array<nr::kernel_contract::Surface,64> surfaces{};
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
            surfaces[i]=s;memory[i].resize(size_t(s.allocation_bytes), 0x35); references[i] = 1;
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

struct Storage {
    std::vector<std::max_align_t> data;
    explicit Storage(size_t bytes):data((bytes+sizeof(std::max_align_t)-1)/sizeof(std::max_align_t)) { if(!bytes)throw std::runtime_error("Zero private allocation"); }
    void *get(){return data.data();}
};
int wmain(int argc,wchar_t **argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    TEST(argc==3 || (argc==4 && !wcscmp(argv[3],L"--reject-unleased")));
    TEST(SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32|LOAD_LIBRARY_SEARCH_USER_DIRS));
    TEST(!wcscmp(argv[2],L"--system-libraries-only"));
    TEST(!GetModuleHandleW(L"openvino.dll"));
    const auto module=LoadLibraryExW(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);TEST(module);
    const auto open=reinterpret_cast<HRESULT(APIENTRY *)(D3D10DDIARG_OPENADAPTER*)>(GetProcAddress(module,"OpenAdapter10_2"));
    const auto configure=reinterpret_cast<BOOL(*)(const char*)>(GetProcAddress(module,"nr_fill_configure"));
    const auto stats=reinterpret_cast<BOOL(*)(NrFillStats*)>(GetProcAddress(module,"nr_fill_stats"));
    TEST(open && configure && stats && !GetProcAddress(module,"D3DKMTCreateDevice"));
    HMODULE owner=nullptr;TEST(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(open),&owner) && owner==module);
    D3DDDI_ADAPTERCALLBACKS callbacks{};callbacks.pfnQueryAdapterInfoCb=query_adapter_cb;
    D3D10_2DDI_ADAPTERFUNCS af{};D3D10DDIARG_OPENADAPTER oa{};oa.pAdapterCallbacks=&callbacks;oa.pAdapterFuncs_2=&af;
    TEST(!configure("CPU") && !configure("GPU"));
    if(argc==4){TEST(!configure("NPU") && open(&oa)==DXGI_ERROR_UNSUPPORTED && !adapter_queries);TEST(FreeLibrary(module));TEST(!GetModuleHandleW(L"openvino.dll"));std::puts(R"json({"unleased_native_adapter_rejected":true,"loaded_dll":true,"passed":true})json");return 0;}
    TEST(configure("NPU") && open(&oa)==S_OK && adapter_queries==1);
    D3DDDI_DEVICECALLBACKS kt{};kt.pfnCreateContextCb=create_context_cb;kt.pfnDestroyContextCb=destroy_context_cb;kt.pfnAllocateCb=allocate_cb;kt.pfnDeallocateCb=deallocate_cb;kt.pfnLockCb=lock_cb;kt.pfnUnlockCb=unlock_cb;kt.pfnRenderCb=render_cb;
    D3D10DDI_CORELAYER_DEVICECALLBACKS um{};um.pfnSetErrorCb=set_error;
    D3D10DDIARG_CALCPRIVATEDEVICESIZE sizing{};sizing.Interface=D3D10_0_DDI_INTERFACE_VERSION;
    Storage ds(af.pfnCalcPrivateDeviceSize(oa.hAdapter,&sizing));D3D10DDIARG_CREATEDEVICE cd{};D3D10DDI_DEVICEFUNCS f{};DXGI_DDI_BASE_FUNCTIONS dx{};
    cd.hDrvDevice.pDrvPrivate=ds.get();cd.hRTDevice.handle=runtime_handle;cd.pKTCallbacks=&kt;cd.pUMCallbacks=&um;cd.pDeviceFuncs=&f;cd.DXGIBaseDDI.pDXGIDDIBaseFunctions=&dx;cd.Interface=D3D10_0_DDI_INTERFACE_VERSION;
    TEST(af.pfnCreateDevice(oa.hAdapter,&cd)==DXGI_STATUS_NO_REDIRECTION);const auto h=cd.hDrvDevice;emulate_copies=true;
    unsigned clears=0;
    for(const auto format:{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_B8G8R8A8_UNORM})for(const UINT levels:{1u,3u}){
        D3D10DDI_MIPINFO mips[3]{};D3D10_DDIARG_SUBRESOURCE_UP initial[3]{};std::vector<unsigned char> pixels[3];
        for(UINT n=0;n<levels;++n){mips[n].TexelWidth=65u>>n;mips[n].TexelHeight=(std::max)(1u,7u>>n);mips[n].TexelDepth=1;pixels[n].assign(size_t(mips[n].TexelWidth*4+11)*mips[n].TexelHeight,0x39);initial[n].pSysMem=pixels[n].data();initial[n].SysMemPitch=mips[n].TexelWidth*4+11;}
        D3D10DDIARG_CREATERESOURCE args{};args.ResourceDimension=D3D10DDIRESOURCE_TEXTURE2D;args.MipLevels=levels;args.ArraySize=args.SampleDesc.Count=1;args.pMipInfoList=mips;args.pInitialDataUP=initial;args.Format=format;args.Usage=D3D10_DDI_USAGE_DEFAULT;args.BindFlags=D3D10_DDI_BIND_RENDER_TARGET;
        Storage rs(f.pfnCalcPrivateResourceSize(h,&args));D3D10DDI_HRESOURCE res{};res.pDrvPrivate=rs.get();f.pfnCreateResource(h,&args,res,{});TEST(last_error==S_OK);
        std::vector<UINT> ids;for(UINT n=1;n<memory.size();++n)if(!memory[n].empty())ids.push_back(n);TEST(ids.size()==levels);
        for(UINT n=0;n<levels;++n){
            D3D10DDIARG_CREATERENDERTARGETVIEW desc{};desc.hDrvResource=res;desc.ResourceDimension=args.ResourceDimension;desc.Format=format;desc.Tex2D.MipSlice=n;desc.Tex2D.ArraySize=1;
            Storage vs(f.pfnCalcPrivateRenderTargetViewSize(h,&desc));D3D10DDI_HRENDERTARGETVIEW view{};view.pDrvPrivate=vs.get();f.pfnCreateRenderTargetView(h,&desc,view,{});TEST(last_error==S_OK);
            const auto before=memory;NrFillStats s0{};TEST(stats(&s0));FLOAT color[4]{std::numeric_limits<float>::infinity(),.25f,.75f,1};f.pfnClearRenderTargetView(h,view,color);NrFillStats rejected{};TEST(stats(&rejected));TEST(last_error==E_INVALIDARG && memory==before && rejected.executions==s0.executions);
            last_error=S_OK;color[0]=.5f;f.pfnClearRenderTargetView(h,view,color);TEST(last_error==S_OK && pending_copies.empty());
            const auto expected=format==DXGI_FORMAT_B8G8R8A8_UNORM?std::array<unsigned char,4>{191,64,128,255}:std::array<unsigned char,4>{128,64,191,255};
            // Allocation metadata comes from the callback contract, never a private UMD struct.
            const auto pitch=surfaces[ids[n]].row_pitch;auto expected_bytes=before[ids[n]];
            for(UINT y=0;y<mips[n].TexelHeight;++y)for(UINT x=0;x<mips[n].TexelWidth;++x)std::memcpy(expected_bytes.data()+size_t(y)*pitch+x*4,expected.data(),4);
            TEST(memory[ids[n]]==expected_bytes);
            for(UINT k=0;k<levels;++k)if(k!=n)TEST(memory[ids[k]]==before[ids[k]]);
            NrFillStats done{};TEST(stats(&done) && done.executions==s0.executions+1);++clears;f.pfnDestroyRenderTargetView(h,view);
        }
        f.pfnDestroyResource(h,res);for(const auto &m:memory)TEST(m.empty());
    }
    f.pfnDestroyDevice(h);TEST(af.pfnCloseAdapter(oa.hAdapter)==S_OK);NrFillStats done{};TEST(stats(&done));
    TEST(clears==8 && done.executions==8 && done.remote_npu_executions==8 && !done.cpu_executions && !done.gpu_executions && !done.npu_executions && !done.live_allocations && !done.live_contexts && !done.cleanup_errors && !contract_error);
    TEST(!std::strcmp(done.last_execution_device,"NPU (local indexed)"));TEST(FreeLibrary(module));TEST(!GetModuleHandleW(L"openvino.dll"));
    std::puts(R"json({"native_loaded_ddi_component":true,"passed":true,"npu_local_executions":8,"clears":8,"cpu_executions":0,"gpu_executions":0,"dll_entry_point_verified":true,"no_openvino_loaded":true,"allocation_callbacks_emulated":true,"registered_adapter_tested":false,"desktop_offloaded":false})json");return 0;
}
