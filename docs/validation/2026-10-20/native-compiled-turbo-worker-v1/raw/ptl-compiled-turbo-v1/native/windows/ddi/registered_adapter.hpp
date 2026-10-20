#pragma once
#include "../kmd/contract.hpp"
#include <charconv>
#include <cstring>
#include <d3dkmthk.h>
#include <dxgi1_4.h>

inline void list_registered_adapters() {
    ComPtr<IDXGIFactory1> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    std::cout << "[";
    for (UINT i = 0;; ++i) {
        ComPtr<IDXGIAdapter1> adapter;
        const auto status = factory->EnumAdapters1(i, &adapter);
        if (status == DXGI_ERROR_NOT_FOUND) break;
        check(status, "EnumAdapters1");
        DXGI_ADAPTER_DESC1 desc{};
        check(adapter->GetDesc1(&desc), "GetDesc1");
        std::ostringstream luid;
        luid << std::hex << std::setfill('0') << std::setw(8)
             << UINT(desc.AdapterLuid.HighPart) << ':' << std::setw(8) << desc.AdapterLuid.LowPart;
        std::cout << (i ? "," : "") << "{\"luid\":" << quote(luid.str())
                  << ",\"vendor_id\":" << desc.VendorId << ",\"device_id\":" << desc.DeviceId
                  << ",\"software\":" << ((desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) ? "true" : "false")
                  << "}";
    }
    std::cout << "]\n";
}

// Select exactly one registered adapter. Never substitute the default adapter,
// WARP, or the process-local software-driver entry point on a failed selection.
inline ComPtr<IDXGIAdapter> registered_adapter(const std::string &text) {
    if (text.size() != 17 || text[8] != ':')
        throw std::runtime_error("adapter LUID must be eight hex digits:eight hex digits");
    UINT high = 0, low = 0;
    const auto a = std::from_chars(text.data(), text.data() + 8, high, 16);
    const auto b = std::from_chars(text.data() + 9, text.data() + 17, low, 16);
    if (a.ec != std::errc{} || a.ptr != text.data() + 8 || b.ec != std::errc{} ||
        b.ptr != text.data() + 17)
        throw std::runtime_error("invalid adapter LUID");
    LUID luid{};
    std::memcpy(&luid.HighPart, &high, sizeof(high));
    luid.LowPart = low;
    ComPtr<IDXGIFactory4> factory;
    check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
    ComPtr<IDXGIAdapter> adapter;
    check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "EnumAdapterByLuid");
    D3DKMT_OPENADAPTERFROMLUID open{};
    open.AdapterLuid = luid;
    if (D3DKMTOpenAdapterFromLuid(&open) != 0)
        throw std::runtime_error("cannot open selected kernel adapter");
    nr::kernel_contract::AdapterInfo identity;
    std::memset(&identity, 0, sizeof(identity));
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = open.hAdapter;
    query.Type = KMTQAITYPE_UMDRIVERPRIVATE;
    query.pPrivateDriverData = &identity;
    query.PrivateDriverDataSize = sizeof(identity);
    const auto status = D3DKMTQueryAdapterInfo(&query);
    D3DKMT_CLOSEADAPTER close{open.hAdapter};
    const auto closed = D3DKMTCloseAdapter(&close);
    const nr::kernel_contract::AdapterInfo expected{};
    if (status != 0 || identity.signature != expected.signature ||
        identity.abi != expected.abi || identity.bytes != expected.bytes || identity.ready != 1)
        throw std::runtime_error("selected adapter is not a ready NPU Render kernel adapter");
    if (closed != 0)
        throw std::runtime_error("kernel adapter query handle cleanup failed");
    return adapter;
}
