// Lab-only UMD discovery guard. The paging adapter is deliberately not a D3D
// renderer. These exports reject opening it without allocating, filling a DDI
// table, or loading an NPU runtime. The normal renderer uses nr_render_umd.
#include <windows.h>
#include <dxgi.h>
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter(void *args) {
    return args ? DXGI_ERROR_UNSUPPORTED : E_INVALIDARG;
}
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10(void *args) {
    return args ? DXGI_ERROR_UNSUPPORTED : E_INVALIDARG;
}
extern "C" __declspec(dllexport) HRESULT APIENTRY OpenAdapter10_2(void *args) {
    return args ? DXGI_ERROR_UNSUPPORTED : E_INVALIDARG;
}
