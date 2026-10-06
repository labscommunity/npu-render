// A bounded graphics-buffer interoperability probe. See interop.md for the
// vendor API references and the distinction between shared allocations and
// proof of a fully copy-free compositor pipeline.
#include "../windows/d3d12_buffers.hpp"
#include "../windows/blend_model.hpp"

int main(int argc, char **argv) {
    std::string stage = "device";
    try {
        size_t width = 64, height = 64, frame_count = 8;
        bool packed_u8 = false, host_tensors = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--packed-u8") packed_u8 = true;
            else if (argument == "--host-tensors") host_tensors = true;
            else if ((argument == "--width" || argument == "--height" || argument == "--frames") && i + 1 < argc) {
                const size_t value = std::stoul(argv[++i]);
                if (argument == "--width") width = value;
                else if (argument == "--height") height = value;
                else frame_count = value;
            } else throw std::invalid_argument("use --width N --height N --frames N [--packed-u8] [--host-tensors]");
        }
        if (!width || width > 4096 || !height || height > 4096 || !frame_count || frame_count > 1000)
            throw std::invalid_argument("invalid extent (1..4096) or frame count (1..1000)");
        const size_t pixels = width * height, elements = pixels * 4;
        const size_t bytes = elements * (packed_u8 ? 1 : sizeof(ov::float16));
        ComPtr<IDXGIFactory1> factory;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "CreateDXGIFactory1");
        ComPtr<ID3D12Device> device;
        std::string adapter_name;
        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> adapter;
            HRESULT status = factory->EnumAdapters1(index, &adapter);
            if (status == DXGI_ERROR_NOT_FOUND) break;
            check(status, "EnumAdapters1");
            DXGI_ADAPTER_DESC1 description{};
            check(adapter->GetDesc1(&description), "GetDesc1");
            if (description.VendorId != 0x8086 || (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
                char name[256];
                WideCharToMultiByte(CP_UTF8, 0, description.Description, -1, name, sizeof(name), nullptr, nullptr);
                adapter_name = name;
                break;
            }
        }
        if (!device) throw std::runtime_error("no Intel D3D12 graphics adapter found");
        stage = "shared_allocations";
        SharedBuffer background(device.Get(), bytes), foreground(device.Get(), bytes), output(device.Get(), bytes);
        auto upload_bg = staging(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD);
        auto upload_fg = staging(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD);
        auto readback = staging(device.Get(), bytes, D3D12_HEAP_TYPE_READBACK);
        Transfers transfers(device.Get());
        float maximum_error = 0;
        std::vector<double> inference_ms, roundtrip_ms;
        // NPU requests/tensors are destroyed before their D3D12 heaps/handles.
        {
            ov::Core core;
            const ov::Shape shape = packed_u8 ? ov::Shape{1, height, width, 4} : ov::Shape{1, 4, height, width};
            const auto element_type = packed_u8 ? ov::element::u8 : ov::element::f16;
            auto model = blend_model(width, height, packed_u8);
            stage = "npu_compile";
            auto compiled = core.compile_model(model, "NPU");
            const auto devices = compiled.get_property(ov::execution_devices);
            if (devices.empty() || std::any_of(devices.begin(), devices.end(), [](const auto &d) {
                return d != "NPU" && d.rfind("NPU.", 0) != 0;
            })) throw std::runtime_error("inference did not report NPU execution");
            auto context = compiled.get_context();
            auto import = [&](HANDLE handle) {
                if (host_tensors) return ov::Tensor(element_type, shape);
                return ov::Tensor(context.create_tensor(element_type, shape,
                    {{"MEM_TYPE", std::string("SHARED_BUF")}, {"MEM_HANDLE", static_cast<void *>(handle)},
                     {"TENSOR_TYPE", std::string("BINDED")}}));
            };
            stage = "nt_handle_import";
            auto remote_bg = import(background.shared.value);
            auto remote_fg = import(foreground.shared.value);
            auto remote_output = import(output.shared.value);
            auto request = compiled.create_infer_request();
            request.set_input_tensor(0, remote_bg);
            request.set_input_tensor(1, remote_fg);
            request.set_output_tensor(remote_output);
            std::vector<ov::float16> b(elements), f(elements);
            std::vector<uint8_t> b8(elements), f8(elements);
            for (size_t frame = 0; frame < frame_count; ++frame) {
                const auto started = Clock::now();
                for (size_t i = 0; i < pixels; ++i) {
                    const float a = float((i + frame * 31) % 257) / 256;
                    for (size_t c = 0; c < 4; ++c) {
                        b[c * pixels + i] = ov::float16(c == 3 ? 1.f : float((i + c * 13 + frame) % 257) / 256);
                        f[c * pixels + i] = ov::float16(c == 3 ? a : a * float((i + c * 37 + frame * 7) % 257) / 256);
                        if (packed_u8) {
                            b8[i * 4 + c] = uint8_t(std::lround(float(b[c * pixels + i]) * 255));
                            f8[i * 4 + c] = uint8_t(std::lround(float(f[c * pixels + i]) * 255));
                        }
                    }
                }
                stage = "gpu_upload";
                upload(upload_bg.Get(), packed_u8 ? static_cast<const void *>(b8.data()) : b.data(), bytes);
                upload(upload_fg.Get(), packed_u8 ? static_cast<const void *>(f8.data()) : f.data(), bytes);
                transfers.begin();
                transfers.barrier(background.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                transfers.barrier(foreground.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                transfers.list->CopyBufferRegion(background.resource.Get(), 0, upload_bg.Get(), 0, bytes);
                transfers.list->CopyBufferRegion(foreground.resource.Get(), 0, upload_fg.Get(), 0, bytes);
                transfers.barrier(background.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                transfers.barrier(foreground.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                transfers.finish(); // Producer completion before NPU reads.
                stage = "npu_infer";
                if (host_tensors) {
                    std::memcpy(remote_bg.data(), packed_u8 ? static_cast<const void *>(b8.data()) : b.data(), bytes);
                    std::memcpy(remote_fg.data(), packed_u8 ? static_cast<const void *>(f8.data()) : f.data(), bytes);
                }
                const auto before_inference = Clock::now();
                request.infer(); // Consumer completion before GPU reads.
                inference_ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - before_inference).count());
                stage = "gpu_readback";
                transfers.begin();
                transfers.barrier(output.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                transfers.list->CopyBufferRegion(readback.Get(), 0, output.resource.Get(), 0, bytes);
                transfers.barrier(output.resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                transfers.finish();
                stage = "pixel_check";
                void *mapped = nullptr;
                D3D12_RANGE read{0, bytes};
                check(readback->Map(0, &read, &mapped), "readback Map");
                const void *values = host_tensors ? remote_output.data() : mapped;
                const auto *actual = static_cast<const ov::float16 *>(values);
                std::string first_mismatch;
                size_t mismatches = 0;
                for (size_t i = 0; i < pixels; ++i) {
                    for (size_t c = 0; c < 4; ++c) {
                        const float foreground_value = packed_u8 ? float(f8[i * 4 + c]) / 255 : float(f[c * pixels + i]);
                        const float background_value = packed_u8 ? float(b8[i * 4 + c]) / 255 : float(b[c * pixels + i]);
                        const float alpha_value = packed_u8 ? float(f8[i * 4 + 3]) / 255 : float(f[3 * pixels + i]);
                        const float expected = foreground_value + (1 - alpha_value) * background_value;
                        const float observed = packed_u8 ? float(static_cast<const uint8_t *>(values)[i * 4 + c]) / 255 : float(actual[c * pixels + i]);
                        const float error = std::abs(observed - expected);
                        if (!std::isfinite(error) || error > 2.f / 255) {
                            ++mismatches;
                            if (first_mismatch.empty()) {
                                std::ostringstream detail;
                                detail << "frame=" << frame << " x=" << i % width << " y=" << i / width
                                       << " channel=" << c << " expected=" << expected << " actual=" << observed;
                                first_mismatch = detail.str();
                            }
                        }
                        maximum_error = std::max(maximum_error, error);
                    }
                }
                D3D12_RANGE no_write{0, 0};
                readback->Unmap(0, &no_write);
                if (mismatches) throw std::runtime_error(first_mismatch + " mismatches=" +
                    std::to_string(mismatches) + " max_error=" + std::to_string(maximum_error));
                roundtrip_ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
            }
        }
        std::cout << "{\"passed\":true,\"adapter\":" << quote(adapter_name)
                  << ",\"execution_device\":\"NPU\",\"width\":" << width << ",\"height\":" << height
                  << ",\"format\":" << quote(packed_u8 ? "BGRA8_NHWC" : "BGRA16F_NCHW")
                  << ",\"frames\":" << frame_count << ","
                  << "\"max_abs_error\":" << maximum_error
                  << ",\"input_and_output_shared\":" << (host_tensors ? "false" : "true")
                  << ",\"host_waited_fences\":true,"
                  << "\"inference_ms\":[";
        for (size_t i = 0; i < inference_ms.size(); ++i) std::cout << (i ? "," : "") << inference_ms[i];
        std::cout << "],\"fixture_upload_infer_readback_check_ms\":[";
        for (size_t i = 0; i < roundtrip_ms.size(); ++i) std::cout << (i ? "," : "") << roundtrip_ms[i];
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cout << "{\"passed\":false,\"stage\":" << quote(stage)
                  << ",\"error\":" << quote(error.what()) << "}\n";
        return 1;
    }
}
