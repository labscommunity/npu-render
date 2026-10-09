// Ordinary registered-adapter D3D11 calls, restricted to the disposable guest.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>
#include <wrl/client.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>
#include "contract.hpp"
#include "../kmd/contract.hpp"
using Microsoft::WRL::ComPtr;
void check(HRESULT status, const char *operation) {
    std::printf("{\"operation\":\"%s\",\"status\":\"%08lx\"}\n", operation, ULONG(status));
    std::fflush(stdout);
    if (FAILED(status)) throw std::runtime_error(operation);
}
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
LUID select_adapter() {
    struct Handles {
        std::vector<D3DKMT_ADAPTERINFO> values;
        ~Handles() { for (const auto &a : values) if (a.hAdapter) { D3DKMT_CLOSEADAPTER close{a.hAdapter}; D3DKMTCloseAdapter(&close); } }
    } handles;
    D3DKMT_ENUMADAPTERS2 list{};
    require(D3DKMTEnumAdapters2(&list) == 0 && list.NumAdapters && list.NumAdapters <= 64, "adapter count");
    handles.values.resize(list.NumAdapters);
    list.pAdapters = handles.values.data();
    require(D3DKMTEnumAdapters2(&list) == 0, "adapter enumeration");
    UINT matches = 0;
    LUID selected{};
    for (const auto &a : handles.values) {
        nr::kernel_contract::AdapterInfo info{};
        std::memset(&info, 0, sizeof(info));
        D3DKMT_QUERYADAPTERINFO query{};
        query.hAdapter = a.hAdapter; query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
        query.pPrivateDriverData = &info; query.PrivateDriverDataSize = sizeof(info);
        const auto status = D3DKMTQueryAdapterInfo(&query);
        const nr::kernel_contract::AdapterInfo expected{};
        if (!status && info.signature == expected.signature && info.abi == expected.abi &&
            info.bytes == expected.bytes && info.ready == 0) { ++matches; selected = a.AdapterLuid; }
    }
    require(matches == 1, "expected exactly one paired laboratory adapter");
    return selected;
}
void exercise(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 48; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    ComPtr<ID3D11Texture2D> target, staging;
    check(device->CreateTexture2D(&desc, nullptr, &target), "create_target");
    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(target.Get(), nullptr, &view), "create_view");
    desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(device->CreateTexture2D(&desc, nullptr, &staging), "create_staging");
    const FLOAT colors[3][4] = {{.2f, .4f, .6f, 1.f}, {1.f, 0.f, 0.f, 1.f}, {0.f, 1.f, 0.f, 1.f}};
    const unsigned char values[3][4] = {{51, 102, 153, 255}, {255, 0, 0, 255}, {0, 255, 0, 255}};
    for (unsigned pass = 0; pass < 3; ++pass) {
        context->ClearRenderTargetView(view.Get(), colors[pass]);
        check(device->GetDeviceRemovedReason(), "clear_device_status");
        context->CopyResource(staging.Get(), target.Get());
        check(device->GetDeviceRemovedReason(), "copy_device_status");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "map_staging");
        bool valid = mapped.pData && mapped.RowPitch >= desc.Width * 4;
        for (UINT y = 0; valid && y < desc.Height; ++y) {
            const auto row = static_cast<const unsigned char *>(mapped.pData) + size_t(y) * mapped.RowPitch;
            for (UINT x = 0; valid && x < desc.Width; ++x) {
                for (UINT c = 0; c < 4; ++c) {
                    const auto channel = format == DXGI_FORMAT_B8G8R8A8_UNORM && c != 1 && c != 3 ? 2 - c : c;
                    if (row[x * 4 + c] != values[pass][channel]) { valid = false; break; }
                }
            }
        }
        context->Unmap(staging.Get(), 0);
        check(device->GetDeviceRemovedReason(), "unmap_device_status");
        require(valid, "clear pixel mismatch");
        std::printf("{\"format\":%u,\"pass\":%u,\"verified_bytes\":12288}\n", UINT(format), pass);
    }
    context->Flush();
}
int main(int argc, char **argv) {
    if (argc != 2 || std::strcmp(argv[1], "--exercise")) return 2;
    if (!nr_fill_guest()) { std::puts("{\"passed\":false,\"error\":\"owned graphics guest required\"}"); return 2; }
    HMODULE retained = nullptr;
    try {
        const auto luid = select_adapter();
        ComPtr<IDXGIFactory4> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "create_factory");
        ComPtr<IDXGIAdapter> adapter;
        check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "select_paired_adapter");
        ComPtr<ID3D11Device> device;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_10_0;
        D3D_FEATURE_LEVEL received{};
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &requested, 1,
                               D3D11_SDK_VERSION, &device, &received, &context), "create_registered_device");
        require(received == requested, "unexpected feature level");
        require(GetModuleHandleExW(0, L"nr_fill_umd.dll", &retained) != FALSE, "expected registered UMD not loaded");
        auto stats = reinterpret_cast<BOOL (*)(NrFillStats *)>(GetProcAddress(retained, "nr_fill_stats"));
        require(stats != nullptr, "UMD diagnostics unavailable");
        exercise(device.Get(), context.Get(), DXGI_FORMAT_B8G8R8A8_UNORM);
        exercise(device.Get(), context.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
        context.Reset(); device.Reset(); adapter.Reset(); factory.Reset();
        NrFillStats result{};
        require(stats(&result) != FALSE, "UMD diagnostics failed");
        std::printf("{\"allocations\":%llu,\"fills\":%llu,\"staging_copy_bytes\":%llu,\"live_allocations\":%llu,\"live_contexts\":%llu,\"cleanup_errors\":%llu,\"rejected\":%llu}\n",
                    result.allocations, result.fills, result.staging_copy_bytes, result.live_allocations,
                    result.live_contexts, result.cleanup_errors, result.rejected);
        require(result.allocations == 4 && result.fills == 6 && result.staging_copy_bytes == 6 * 12288 &&
                !result.live_allocations && !result.live_contexts && !result.cleanup_errors && !result.rejected, "unexpected UMD counters");
        FreeLibrary(retained);
        std::puts("{\"passed\":true,\"npu_execution_tested\":false,\"desktop_offload_tested\":false}");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "registered clear failed: %s\n", e.what());
        if (retained) {
            auto stats = reinterpret_cast<BOOL (*)(NrFillStats *)>(GetProcAddress(retained, "nr_fill_stats"));
            NrFillStats result{};
            if (stats && stats(&result))
                std::printf("{\"failed\":true,\"rejected\":%llu,\"last_unsupported_offset\":%llu,\"cleanup_errors\":%llu}\n",
                            result.rejected, result.last_unsupported_offset, result.cleanup_errors);
            FreeLibrary(retained);
        }
        return 1;
    }
}
