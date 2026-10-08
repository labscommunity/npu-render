// Graphics-owned, row-major texture -> NPU output -> native D3D11 consumer.
// This probes resource interoperability; it does not select a DWM adapter.
#include "../windows/typed_texture.hpp"

int main(int argc, char **argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    struct Stage : std::string {
        using std::string::string;
        Stage &operator=(const char *value) {
            std::string::operator=(value);
            std::cout << "{\"stage\":" << quote(*this) << "}\n" << std::flush;
            return *this;
        }
    } stage = "arguments";
    Diagnostics diagnostics;
    try {
        bool debug = false, buffer_control = false, rgba = false, gpu_map = true, legacy = false,
             sample = false;
        UINT width = 64, height = 48;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--debug")
                debug = true;
            else if (argument == "--buffer-control")
                buffer_control = true;
            else if (argument == "--rgba")
                rgba = true;
            else if (argument == "--sample")
                sample = true;
            else if (argument == "--diagnostic-legacy")
                legacy = true;
            else if (argument == "--diagnostic-npu-map")
                gpu_map = false;
            else if (argument == "--ddi-import" || argument == "--direct-import")
                throw std::runtime_error(
                    "unqualified resource import is blocked before device creation");
            else if ((argument == "--width" || argument == "--height") && i + 1 < argc) {
                const std::string value(argv[++i]);
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("invalid dimension");
                const auto n = std::stoull(value);
                if (!n || n > 4096)
                    throw std::runtime_error("dimension outside 1..4096");
                if (argument == "--width")
                    width = UINT(n);
                else
                    height = UINT(n);
            } else
                throw std::runtime_error(
                    "use [--debug] [--width N --height N] [--rgba | --buffer-control] "
                    "[--diagnostic-legacy | --diagnostic-npu-map]");
        }
        if (buffer_control && (rgba || !gpu_map || legacy || sample))
            throw std::runtime_error("texture variant and buffer control are exclusive");
        if (width % 64)
            throw std::runtime_error("texture width must align to 256-byte rows");
        const bool mapped_alias = !buffer_control;
        const UINT frames = 16;
        const size_t bytes = size_t(width) * height * 4;
        stage = "exact_gpu_device";
        ov::Core core;
        const auto id = core.get_property("GPU", ov::device::luid);
        LUID luid{};
        static_assert(sizeof(luid) == ov::device::LUID::MAX_LUID_SIZE);
        std::memcpy(&luid, id.luid.data(), sizeof(luid));
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter1> adapter;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "exact GPU adapter");
        if (debug) {
            ComPtr<ID3D12Debug> layer;
            check(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)), "D3D12 debug layer");
            layer->EnableDebugLayer();
        }
        ComPtr<ID3D12Device> gpu;
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu)),
              "D3D12 device");
        gpu.As(&diagnostics.twelve);
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        check(gpu->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
              "D3D12 capabilities");
        if (!buffer_control && !options.CrossAdapterRowMajorTextureSupported)
            throw std::runtime_error(
                "graphics driver cannot bind cross-adapter row-major textures");
        ComPtr<ID3D11Device> eleven;
        ComPtr<ID3D11DeviceContext> context;
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                                debug ? D3D11_CREATE_DEVICE_DEBUG : 0, &level, 1, D3D11_SDK_VERSION,
                                &eleven, nullptr, &context),
              "D3D11 device");
        eleven.As(&diagnostics.eleven);
        ComPtr<ID3D12CompatibilityDevice> compatibility;
        check(gpu.As(&compatibility), "D3D12 compatibility interface");
        stage = "create_typed_cross_adapter_texture";
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Format = rgba ? DXGI_FORMAT_R8G8B8A8_UNORM : DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags =
            D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D11_RESOURCE_FLAGS flags{};
        flags.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        flags.BindFlags |= D3D11_BIND_RENDER_TARGET;
        flags.MiscFlags =
            D3D11_RESOURCE_MISC_SHARED | (legacy ? 0 : D3D11_RESOURCE_MISC_SHARED_NTHANDLE);
        ComPtr<ID3D12Resource> texture;
        Handle shared;
        std::unique_ptr<SharedBuffer> buffer;
        ComPtr<ID3D12Resource> buffer_readback;
        std::unique_ptr<Transfers> transfers;
        ComPtr<ID3D11Texture2D> imported, readback;
        ShaderConsumer consumer;
        HANDLE output_handle = nullptr;
        if (buffer_control) {
            buffer = std::make_unique<SharedBuffer>(gpu.Get(), bytes);
            output_handle = buffer->shared.value;
            buffer_readback = staging(gpu.Get(), bytes, D3D12_HEAP_TYPE_READBACK);
            transfers = std::make_unique<Transfers>(gpu.Get());
        } else {
            check(compatibility->CreateSharedResource(
                      &heap, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER, &desc,
                      D3D12_RESOURCE_STATE_COMMON, nullptr, &flags,
                      legacy ? D3D12_COMPATIBILITY_SHARED_FLAG_NON_NT_HANDLE
                             : D3D12_COMPATIBILITY_SHARED_FLAG_NONE,
                      nullptr, nullptr, IID_PPV_ARGS(&texture)),
                  "CreateSharedResource(row-major texture)");
            if (legacy) {
                check(compatibility->ReflectSharedProperties(
                          texture.Get(), D3D12_REFLECT_SHARED_PROPERTY_NON_NT_SHARED_HANDLE,
                          &output_handle, sizeof(output_handle)),
                      "legacy texture handle");
            } else {
                check(gpu->CreateSharedHandle(texture.Get(), nullptr, GENERIC_ALL, nullptr,
                                              &shared.value),
                      "CreateSharedHandle(texture)");
                output_handle = shared.value;
            }
            stage = "native_d3d11_open";
            ComPtr<ID3D11Device1> eleven1;
            check(eleven.As(&eleven1), "D3D11.1 device");
            check(legacy ? eleven->OpenSharedResource(output_handle, IID_PPV_ARGS(&imported))
                         : eleven1->OpenSharedResource1(output_handle, IID_PPV_ARGS(&imported)),
                  "OpenSharedResource1(typed texture)");
            D3D11_TEXTURE2D_DESC actual{};
            imported->GetDesc(&actual);
            if (actual.Width != width || actual.Height != height || actual.Format != desc.Format ||
                actual.MipLevels != 1 || actual.ArraySize != 1 || actual.SampleDesc.Count != 1)
                throw std::runtime_error("D3D11 resource description mismatch");
            actual.Usage = D3D11_USAGE_STAGING;
            actual.BindFlags = actual.MiscFlags = 0;
            actual.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            check(eleven->CreateTexture2D(&actual, nullptr, &readback), "D3D11 readback");
            if (sample)
                consumer.create(eleven.Get(), imported.Get());
        }
        diagnostics.dump();
        const auto npu_driver =
            std::to_string(core.get_property("NPU", ov::intel_npu::driver_version));
        TextureAlias alias;
        if (!buffer_control) {
            stage = "npu_texture_page_alias";
            const auto allocation = gpu->GetResourceAllocationInfo(0, 1, &desc);
            alias.create(core, output_handle, allocation.SizeInBytes, gpu_map, legacy);
            output_handle = alias.shared;
        }
        unsigned maximum_error = 0;
        {
            stage = "npu_import_and_compile";
            const auto compile_started = Clock::now();
            auto compiled =
                core.compile_model(blend_model(width, height, true, 4, "convolution"), "NPU",
                                   ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
            std::cout
                << "{\"compile_ms\":"
                << std::chrono::duration<double, std::milli>(Clock::now() - compile_started).count()
                << "}\n"
                << std::flush;
            const auto devices = compiled.get_property(ov::execution_devices);
            if (devices.empty() || std::any_of(devices.begin(), devices.end(), [](const auto &d) {
                    return d != "NPU" && d.rfind("NPU.", 0) != 0;
                }))
                throw std::runtime_error("NPU execution identity mismatch");
            auto output = compiled.get_context().create_tensor(
                ov::element::u8, {1, height, width, 4},
                {{"MEM_TYPE", std::string("SHARED_BUF")},
                 {"MEM_HANDLE", static_cast<void *>(output_handle)},
                 {"TENSOR_TYPE", std::string("BINDED")}});
            auto request = compiled.create_infer_request();
            request.set_output_tensor(output);
            auto background = request.get_input_tensor(0), foreground = request.get_input_tensor(1);
            // Keep the oracle in ordinary CPU memory. Device input allocations can
            // be write-combined; reading them byte by byte is a different workload.
            std::vector<uint8_t> background_fixture(bytes), foreground_fixture(bytes);
            auto *bg = background_fixture.data();
            auto *fg = foreground_fixture.data();
            MEMORY_BASIC_INFORMATION input_memory{};
            VirtualQuery(background.data(), &input_memory, sizeof(input_memory));
            std::cout << "{\"input_memory_protection\":" << input_memory.Protect << "}\n"
                      << std::flush;
            for (UINT frame = 0; frame < frames; ++frame) {
                for (size_t i = 0; i < bytes / 4; ++i) {
                    const auto a = uint8_t((i * 19 + frame * 31) % 256);
                    for (size_t c = 0; c < 4; ++c) {
                        bg[i * 4 + c] = c == 3 ? 255 : uint8_t((i + c * 37 + frame * 17) % 256);
                        fg[i * 4 + c] =
                            c == 3 ? a : uint8_t(((i + c * 23 + frame * 7) % 256) * a / 255);
                    }
                }
                stage = "npu_write_texture";
                std::memcpy(background.data(), bg, bytes);
                std::memcpy(foreground.data(), fg, bytes);
                const auto infer_started = Clock::now();
                request.infer(); // Complete the NPU write before native GPU consumption.
                const auto infer_finished = Clock::now();
                stage = "native_d3d11_readback";
                D3D11_MAPPED_SUBRESOURCE map{};
                if (buffer_control) {
                    stage = "native_d3d12_buffer_readback";
                    transfers->begin();
                    transfers->barrier(buffer->resource.Get(), D3D12_RESOURCE_STATE_COMMON,
                                       D3D12_RESOURCE_STATE_COPY_SOURCE);
                    transfers->list->CopyBufferRegion(buffer_readback.Get(), 0,
                                                      buffer->resource.Get(), 0, bytes);
                    transfers->barrier(buffer->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
                                       D3D12_RESOURCE_STATE_COMMON);
                    transfers->finish();
                    D3D12_RANGE range{0, bytes};
                    check(buffer_readback->Map(0, &range, &map.pData), "D3D12 buffer Map");
                    map.RowPitch = width * 4;
                } else {
                    if (sample)
                        consumer.draw(context.Get());
                    context->CopyResource(readback.Get(),
                                          sample ? consumer.target.Get() : imported.Get());
                    check(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map), "D3D11 Map");
                }
                unsigned error = 0;
                const auto map_finished = Clock::now();
                for (UINT y = 0; y < height; ++y)
                    for (UINT x = 0; x < width * 4; ++x) {
                        const size_t i = size_t(y) * width * 4 + x;
                        // The denominator is odd, so integer source-over has no half ties.
                        const int expected = std::min(
                            255,
                            int(fg[i]) + (int(bg[i]) * (255 - fg[(i / 4) * 4 + 3]) + 127) / 255);
                        const int observed =
                            static_cast<const uint8_t *>(map.pData)[size_t(y) * map.RowPitch + x];
                        error = std::max(error, unsigned(std::abs(expected - observed)));
                    }
                if (buffer_control) {
                    D3D12_RANGE no_write{0, 0};
                    buffer_readback->Unmap(0, &no_write);
                } else
                    context->Unmap(readback.Get(), 0);
                diagnostics.dump();
                maximum_error = std::max(maximum_error, error);
                std::cout << "{\"frame\":" << frame << ",\"max_byte_error\":" << error
                          << ",\"npu_infer_ms\":"
                          << std::chrono::duration<double, std::milli>(infer_finished -
                                                                       infer_started)
                                 .count()
                          << ",\"gpu_map_ms\":"
                          << std::chrono::duration<double, std::milli>(map_finished -
                                                                       infer_finished)
                                 .count()
                          << ",\"oracle_ms\":"
                          << std::chrono::duration<double, std::milli>(Clock::now() - map_finished)
                                 .count()
                          << "}\n"
                          << std::flush;
                if (error > 1)
                    throw std::runtime_error(
                        "native graphics pixels differ from independent oracle");
            }
        }
        alias.close();
        if (alias.cleanup_failures)
            throw std::runtime_error("texture alias cleanup failed");
        if (diagnostics.errors)
            throw std::runtime_error("graphics diagnostics reported errors");
        std::cout << "{\"passed\":true,\"full_desktop_offload\":false,\"npu_output_shared\":true,"
                     "\"host_inputs\":true,\"host_synchronized\":true,\"frames\":"
                  << frames << ",\"execution_device\":\"NPU\",\"npu_driver\":" << quote(npu_driver)
                  << ",\"host_input_copy_bytes\":" << uint64_t(bytes) * 2 * frames
                  << ",\"host_output_copy_bytes\":0,\"consumer_api\":"
                  << quote(buffer_control ? "D3D12_BUFFER" : "D3D11_TEXTURE")
                  << ",\"mapped_alias\":" << (mapped_alias ? "true" : "false")
                  << ",\"mapping_owner\":" << quote(gpu_map ? "GPU" : "NPU")
                  << ",\"graphics_debug_enabled\":" << (debug ? "true" : "false")
                  << ",\"graphics_debug_errors\":" << diagnostics.errors << ",\"width\":" << width
                  << ",\"gpu_validation_draws\":" << (sample ? frames : 0)
                  << ",\"height\":" << height << ",\"format\":" << quote(rgba ? "RGBA" : "BGRA")
                  << ",\"max_byte_error\":" << maximum_error << "}\n";
        return 0;
    } catch (const std::exception &error) {
        diagnostics.dump();
        std::cout << "{\"passed\":false,\"full_desktop_offload\":false,\"stage\":" << quote(stage)
                  << ",\"error\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
