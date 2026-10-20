// Calls the exported normal-loader ABI with adversarial adapter callbacks.
// It does not enumerate a registered adapter, create a D3D device, or run NPU work.
#define NOMINMAX
#include <windows.h>
#include <d3d10umddi.h>
#include <cstring>
#include <iostream>
#include "../kmd/contract.hpp"
namespace {
int mode = 0;
HRESULT APIENTRY query(HANDLE, const D3DDDICB_QUERYADAPTERINFO *args) {
    if (mode == 0) return E_ACCESSDENIED;
    if (mode == 1) return S_OK; // Malformed success without output.
    if (!args || args->PrivateDriverDataSize != sizeof(nr::kernel_contract::AdapterInfo))
        return E_INVALIDARG;
    nr::kernel_contract::AdapterInfo info{};
    info.ready = mode == 2 ? 0 : 1;
    if (mode == 3) ++info.abi;
    if (mode == 4) ++info.bytes;
    if (mode == 5) ++info.signature;
    if (mode == 6) info.ready = 2;
    std::memcpy(args->pPrivateDriverData, &info, sizeof(info));
    return S_OK;
}
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryExA(argv[1], nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!module) { std::cerr << "LoadLibrary failed " << GetLastError() << '\n'; return 1; }
    auto open = reinterpret_cast<PFND3D10DDI_OPENADAPTER>(GetProcAddress(module, "OpenAdapter10_2"));
    bool passed = open && !GetProcAddress(module, "D3DKMTCreateDevice") &&
                  !GetProcAddress(module, "D3DKMTOpenAdapterFromHdc");
    D3DDDI_ADAPTERCALLBACKS callbacks{};
    callbacks.pfnQueryAdapterInfoCb = query;
    for (mode = 0; passed && mode <= 7; ++mode) {
        D3D10_2DDI_ADAPTERFUNCS functions{};
        D3D10DDIARG_OPENADAPTER args{};
        args.pAdapterFuncs_2 = &functions;
        args.pAdapterCallbacks = &callbacks;
        HRESULT hr = open(&args);
        if (mode < 7)
            passed = FAILED(hr) && !args.hAdapter.pDrvPrivate && !functions.pfnCreateDevice;
        else
            passed = SUCCEEDED(hr) && functions.pfnCreateDevice && functions.pfnGetCaps &&
                     functions.pfnCloseAdapter && SUCCEEDED(functions.pfnCloseAdapter(args.hAdapter));
    }
    FreeLibrary(module);
    std::cout << "registered UMD contract " << (passed ? "passed" : "FAILED") << '\n';
    return passed ? 0 : 1;
}
