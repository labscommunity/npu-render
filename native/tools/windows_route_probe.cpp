// Query the actual OpenVINO device LUID directly, including adapters omitted
// from workload-filtered enumeration. Never substitute the default adapter.
#include "../windows/d3d12_buffers.hpp"
#include <d3d11.h>
#include <initguid.h>
#include <dxcore.h>

int main() {
    try {
        ov::Core runtime;
        ComPtr<IDXGIFactory4> dxgi;
        ComPtr<IDXCoreAdapterFactory> dxcore;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&dxgi)),"DXGI factory");
        check(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&dxcore)),"DXCore factory");
        std::cout << "{\"scope\":\"direct adapter identity and device creation; no shader execution or desktop redirection\",\"devices\":[";
        bool first = true;
        for (const std::string device : {"GPU","NPU"}) {
            std::cout << (first ? "" : ",") << "{\"openvino_device\":" << quote(device);
            first = false;
            try {
                const auto ov_luid = runtime.get_property(device,ov::device::luid);
                LUID luid{};
                static_assert(sizeof(luid) == ov::device::LUID::MAX_LUID_SIZE);
                std::memcpy(&luid,ov_luid.luid.data(),sizeof(luid));
                std::ostringstream formatted;
                formatted << "luid_0x" << std::hex << std::setfill('0') << std::setw(8) << uint32_t(luid.HighPart)
                          << "_0x" << std::setw(8) << luid.LowPart;
                std::cout << ",\"instance_luid\":" << quote(formatted.str());
                ComPtr<IDXGIAdapter1> graphics;
                const HRESULT graphics_status = dxgi->EnumAdapterByLuid(luid,IID_PPV_ARGS(&graphics));
                std::cout << ",\"dxgi_lookup_hresult\":" << uint32_t(graphics_status);
                if (SUCCEEDED(graphics_status)) {
                    DXGI_ADAPTER_DESC1 desc{}; check(graphics->GetDesc1(&desc),"DXGI description");
                    if (desc.AdapterLuid.HighPart != luid.HighPart || desc.AdapterLuid.LowPart != luid.LowPart)
                        throw std::runtime_error("DXGI returned a different adapter");
                    ComPtr<ID3D11Device> d3d11;
                    D3D_FEATURE_LEVEL level{};
                    const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_11_0;
                    const HRESULT created = D3D11CreateDevice(graphics.Get(),D3D_DRIVER_TYPE_UNKNOWN,nullptr,
                        D3D11_CREATE_DEVICE_BGRA_SUPPORT,&requested,1,D3D11_SDK_VERSION,&d3d11,&level,nullptr);
                    std::cout << ",\"d3d11_create_hresult\":" << uint32_t(created);
                    if (SUCCEEDED(created)) std::cout << ",\"d3d11_feature_level\":" << uint32_t(level);
                } else std::cout << ",\"d3d11_create_hresult\":null";
                ComPtr<IDXCoreAdapter> compute;
                const HRESULT core_status = dxcore->GetAdapterByLuid(luid,IID_PPV_ARGS(&compute));
                std::cout << ",\"dxcore_lookup_hresult\":" << uint32_t(core_status);
                if (SUCCEEDED(core_status)) {
                    LUID actual{};
                    check(compute->GetProperty(DXCoreAdapterProperty::InstanceLuid,sizeof(actual),&actual),"DXCore LUID");
                    if (actual.HighPart != luid.HighPart || actual.LowPart != luid.LowPart)
                        throw std::runtime_error("DXCore returned a different adapter");
                    std::cout << ",\"graphics_attribute\":" << (compute->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS) ? "true" : "false")
                              << ",\"core_compute_attribute\":" << (compute->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE) ? "true" : "false")
                              << ",\"generic_ml_attribute\":" << (compute->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML) ? "true" : "false")
                              << ",\"d3d12_device_checks\":[";
                    const std::pair<const char *,D3D_FEATURE_LEVEL> levels[] = {
                        {"generic_ml",D3D_FEATURE_LEVEL_1_0_GENERIC},
                        {"core_compute",D3D_FEATURE_LEVEL_1_0_CORE},
                        {"graphics_11_0",D3D_FEATURE_LEVEL_11_0}};
                    for (size_t i = 0; i < 3; ++i) {
                        ComPtr<ID3D12Device> created;
                        const HRESULT status = D3D12CreateDevice(compute.Get(),levels[i].second,IID_PPV_ARGS(&created));
                        std::cout << (i ? "," : "") << "{\"requested\":" << quote(levels[i].first)
                                  << ",\"hresult\":" << uint32_t(status);
                        if (SUCCEEDED(status)) {
                            const LUID actual_device = created->GetAdapterLuid();
                            const bool matches = actual_device.HighPart == luid.HighPart && actual_device.LowPart == luid.LowPart;
                            std::cout << ",\"adapter_matches\":" << (matches ? "true" : "false");
                            D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
                            const HRESULT queried = created->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL,&model,sizeof(model));
                            std::cout << ",\"shader_model_query_hresult\":" << uint32_t(queried);
                            if (SUCCEEDED(queried)) std::cout << ",\"shader_model\":" << uint32_t(model.HighestShaderModel);
                        }
                        std::cout << '}';
                    }
                    std::cout << ']';
                }
            } catch (const std::exception &error) { std::cout << ",\"error\":" << quote(error.what()); }
            std::cout << '}';
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
