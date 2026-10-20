// Inspect advertised NPU interfaces without dispatching shaders or ML graphs.
#include "../windows/d3d12_buffers.hpp"
#include <initguid.h>
#include <dxcore.h>

int main(int argc, char **argv) {
    try {
        const bool d3d_only = argc == 2 && std::string(argv[1]) == "--d3d-ml";
        if (argc > 1 && !d3d_only) throw std::invalid_argument("use --d3d-ml or no arguments");
        ComPtr<IDXCoreAdapterFactory> factory;
        check(DXCoreCreateAdapterFactory(IID_PPV_ARGS(&factory)), "DXCore factory");
        ComPtr<IDXCoreAdapterList> adapters;
        if (d3d_only) {
            const GUID filter = DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML;
            check(factory->CreateAdapterList(1, &filter, IID_PPV_ARGS(&adapters)), "ML adapter list");
        } else {
            ComPtr<IDXCoreAdapterFactory1> extended;
            check(factory.As(&extended), "DXCore workload factory");
            check(extended->CreateAdapterListByWorkload(DXCoreWorkload::MachineLearning,
                DXCoreRuntimeFilterFlags::None, DXCoreHardwareTypeFilterFlags::NPU,
                IID_PPV_ARGS(&adapters)), "NPU workload list");
        }
        unsigned npu_count = 0;
        std::cout << "{\"scope\":\"advertised interfaces and device creation only; no shader or graph execution\",\"filter\":"
                  << quote(d3d_only ? "d3d12_generic_ml" : "npu_workload_any_runtime") << ",\"adapters\":[";
        for (uint32_t i = 0; i < adapters->GetAdapterCount(); ++i) {
            ComPtr<IDXCoreAdapter> adapter;
            check(adapters->GetAdapter(i, IID_PPV_ARGS(&adapter)), "get adapter");
            size_t length = 0;
            check(adapter->GetPropertySize(DXCoreAdapterProperty::DriverDescription, &length), "description size");
            if (!length || length > 65536) throw std::runtime_error("invalid adapter description size");
            std::vector<char> description(length + 1, 0);
            check(adapter->GetProperty(DXCoreAdapterProperty::DriverDescription, length, description.data()), "description");
            uint64_t version = 0;
            check(adapter->GetProperty(DXCoreAdapterProperty::DriverVersion, sizeof(version), &version), "driver version");
            LUID luid{};
            const HRESULT luid_status = adapter->GetProperty(DXCoreAdapterProperty::InstanceLuid, sizeof(luid), &luid);
            std::ostringstream luid_name;
            luid_name << "luid_0x" << std::hex << std::setfill('0') << std::setw(8) << uint32_t(luid.HighPart)
                      << "_0x" << std::setw(8) << luid.LowPart;
            bool hardware = false;
            const HRESULT hardware_status = adapter->GetProperty(DXCoreAdapterProperty::IsHardware, sizeof(hardware), &hardware);
            const bool npu = adapter->IsAttributeSupported(DXCORE_HARDWARE_TYPE_ATTRIBUTE_NPU);
            const bool core = adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_CORE_COMPUTE);
            const bool graphics = adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GRAPHICS);
            const bool ml = adapter->IsAttributeSupported(DXCORE_ADAPTER_ATTRIBUTE_D3D12_GENERIC_ML);
            std::ostringstream driver;
            driver << (version >> 48) << '.' << ((version >> 32) & 65535)
                   << '.' << ((version >> 16) & 65535) << '.' << (version & 65535);
            std::cout << (i ? "," : "") << "{\"description\":" << quote(description.data())
                      << ",\"dxcore_driver_version\":" << quote(driver.str())
                      << ",\"instance_luid\":" << (SUCCEEDED(luid_status) ? quote(luid_name.str()) : "null")
                      << ",\"luid_query_hresult\":" << uint32_t(luid_status)
                      << ",\"hardware\":" << (SUCCEEDED(hardware_status) ? (hardware ? "true" : "false") : "null")
                      << ",\"hardware_query_hresult\":" << uint32_t(hardware_status)
                      << ",\"npu\":" << (npu ? "true" : "false")
                      << ",\"core_compute\":" << (core ? "true" : "false")
                      << ",\"generic_ml\":" << (ml ? "true" : "false")
                      << ",\"graphics\":" << (graphics ? "true" : "false");
            if (npu) {
                ++npu_count;
                if (ml) {
                    ComPtr<ID3D12Device> generic;
                    const HRESULT created = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_1_0_GENERIC,
                                                              IID_PPV_ARGS(&generic));
                    std::cout << ",\"generic_create_hresult\":" << uint32_t(created);
                }
                if (core) {
                    ComPtr<ID3D12Device> compute;
                    const HRESULT result = D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_1_0_CORE,
                                                             IID_PPV_ARGS(&compute));
                    std::cout << ",\"core_create_hresult\":" << uint32_t(result);
                    if (SUCCEEDED(result)) {
                        D3D12_FEATURE_DATA_SHADER_MODEL model{D3D_SHADER_MODEL_6_0};
                        const HRESULT queried = compute->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &model, sizeof(model));
                        std::cout << ",\"shader_model_query_hresult\":" << uint32_t(queried)
                                  << ",\"shader_model\":" << uint32_t(model.HighestShaderModel);
                    }
                }
            }
            std::cout << '}';
        }
        std::cout << "],\"npu_count\":" << npu_count << "}\n";
        return npu_count ? 0 : 1;
    } catch (const std::exception &error) {
        std::cerr << "{\"error\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
