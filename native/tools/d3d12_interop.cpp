// A bounded graphics-buffer interoperability probe. See interop.md for the
// vendor API references and the distinction between shared allocations and
// proof of a fully copy-free compositor pipeline.
#define NOMINMAX
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl.h>
#include <openvino/openvino.hpp>
#include <openvino/opsets/opset13.hpp>
#include <openvino/runtime/properties.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

void check(HRESULT code, const char *operation) {
    if (FAILED(code)) {
        std::ostringstream error;
        error << operation << " failed with HRESULT 0x" << std::hex << uint32_t(code);
        throw std::runtime_error(error.str());
    }
}

std::string quote(const std::string &value) {
    std::ostringstream text;
    text << '"';
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') text << '\\' << c;
        else if (c < 32) text << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
        else text << c;
    }
    return text.str() + '"';
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value) CloseHandle(value); }
    Handle() = default;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};

D3D12_RESOURCE_DESC buffer_description(size_t bytes, D3D12_RESOURCE_FLAGS flags) {
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = bytes;
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = flags;
    return desc;
}

struct SharedBuffer {
    ComPtr<ID3D12Heap> heap;
    ComPtr<ID3D12Resource> resource;
    Handle shared;

    SharedBuffer(ID3D12Device *device, size_t bytes) {
        const size_t alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
        D3D12_HEAP_DESC desc{};
        desc.SizeInBytes = (bytes + alignment - 1) & ~(alignment - 1);
        desc.Alignment = alignment;
        desc.Properties.Type = D3D12_HEAP_TYPE_CUSTOM;
        desc.Properties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_NOT_AVAILABLE;
        desc.Properties.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
        desc.Properties.CreationNodeMask = desc.Properties.VisibleNodeMask = 1;
        desc.Flags = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER;
        check(device->CreateHeap(&desc, IID_PPV_ARGS(&heap)), "CreateHeap(cross-adapter)");
        auto resource_desc = buffer_description(bytes, D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER |
                                                         D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
        check(device->CreatePlacedResource(heap.Get(), 0, &resource_desc,
                                          D3D12_RESOURCE_STATE_COMMON, nullptr,
                                          IID_PPV_ARGS(&resource)), "CreatePlacedResource");
        check(device->CreateSharedHandle(heap.Get(), nullptr, GENERIC_ALL, nullptr, &shared.value),
              "CreateSharedHandle(heap)");
    }
};

ComPtr<ID3D12Resource> staging(ID3D12Device *device, size_t bytes, D3D12_HEAP_TYPE type) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = type;
    heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    auto desc = buffer_description(bytes, D3D12_RESOURCE_FLAG_NONE);
    ComPtr<ID3D12Resource> result;
    check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                          type == D3D12_HEAP_TYPE_UPLOAD ?
                                              D3D12_RESOURCE_STATE_GENERIC_READ : D3D12_RESOURCE_STATE_COPY_DEST,
                                          nullptr, IID_PPV_ARGS(&result)), "CreateCommittedResource(staging)");
    return result;
}

struct Transfers {
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    Handle event;
    uint64_t serial = 0;

    explicit Transfers(ID3D12Device *device) {
        D3D12_COMMAND_QUEUE_DESC desc{};
        desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        check(device->CreateCommandQueue(&desc, IID_PPV_ARGS(&queue)), "CreateCommandQueue");
        check(device->CreateCommandAllocator(desc.Type, IID_PPV_ARGS(&allocator)), "CreateCommandAllocator");
        check(device->CreateCommandList(0, desc.Type, allocator.Get(), nullptr,
                                        IID_PPV_ARGS(&list)), "CreateCommandList");
        check(list->Close(), "initial command-list close");
        check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)), "CreateFence");
        event.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event.value) throw std::runtime_error("CreateEvent failed");
    }

    void begin() {
        check(allocator->Reset(), "allocator reset");
        check(list->Reset(allocator.Get(), nullptr), "command-list reset");
    }

    void barrier(ID3D12Resource *resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
        D3D12_RESOURCE_BARRIER transition{};
        transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        transition.Transition.pResource = resource;
        transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        transition.Transition.StateBefore = before;
        transition.Transition.StateAfter = after;
        list->ResourceBarrier(1, &transition);
    }

    void finish() {
        check(list->Close(), "command-list close");
        ID3D12CommandList *commands[] = {list.Get()};
        queue->ExecuteCommandLists(1, commands);
        check(queue->Signal(fence.Get(), ++serial), "queue signal");
        check(fence->SetEventOnCompletion(serial, event.value), "fence event");
        if (WaitForSingleObject(event.value, 10000) != WAIT_OBJECT_0)
            throw std::runtime_error("GPU transfer fence did not complete within 10 seconds");
    }
};

void upload(ID3D12Resource *resource, const std::vector<ov::float16> &values) {
    void *mapped = nullptr;
    D3D12_RANGE no_read{0, 0};
    check(resource->Map(0, &no_read, &mapped), "upload Map");
    const size_t bytes = values.size() * sizeof(ov::float16);
    std::memcpy(mapped, values.data(), bytes);
    D3D12_RANGE written{0, bytes};
    resource->Unmap(0, &written);
}

int main() {
    std::string stage = "device";
    try {
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
        constexpr size_t side = 64, pixels = side * side, elements = pixels * 4;
        constexpr size_t bytes = elements * sizeof(ov::float16);
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
            namespace op = ov::opset13;
            const ov::Shape shape{1, 4, side, side};
            auto bg = std::make_shared<op::Parameter>(ov::element::f16, shape);
            auto fg = std::make_shared<op::Parameter>(ov::element::f16, shape);
            auto alpha = std::make_shared<op::Gather>(fg,
                op::Constant::create(ov::element::i64, {1}, {3}), op::Constant::create(ov::element::i64, {}, {1}));
            auto result = std::make_shared<op::Add>(fg, std::make_shared<op::Multiply>(bg,
                std::make_shared<op::Subtract>(op::Constant::create(ov::element::f16, {}, {1.f}), alpha)));
            auto model = std::make_shared<ov::Model>(ov::OutputVector{result}, ov::ParameterVector{bg, fg});
            stage = "npu_compile";
            auto compiled = core.compile_model(model, "NPU");
            const auto devices = compiled.get_property(ov::execution_devices);
            if (devices.empty() || std::any_of(devices.begin(), devices.end(), [](const auto &d) {
                return d != "NPU" && d.rfind("NPU.", 0) != 0;
            })) throw std::runtime_error("inference did not report NPU execution");
            auto context = compiled.get_context();
            auto import = [&](HANDLE handle) {
                return context.create_tensor(ov::element::f16, shape,
                    {{"MEM_TYPE", std::string("SHARED_BUF")}, {"MEM_HANDLE", static_cast<void *>(handle)},
                     {"TENSOR_TYPE", std::string("BINDED")}});
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
            for (size_t frame = 0; frame < 8; ++frame) {
                const auto started = Clock::now();
                for (size_t i = 0; i < pixels; ++i) {
                    const float a = float((i + frame * 31) % 257) / 256;
                    for (size_t c = 0; c < 4; ++c) {
                        b[c * pixels + i] = ov::float16(c == 3 ? 1.f : float((i + c * 13 + frame) % 257) / 256);
                        f[c * pixels + i] = ov::float16(c == 3 ? a : a * float((i + c * 37 + frame * 7) % 257) / 256);
                    }
                }
                stage = "gpu_upload";
                upload(upload_bg.Get(), b);
                upload(upload_fg.Get(), f);
                transfers.begin();
                transfers.barrier(background.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                transfers.barrier(foreground.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
                transfers.list->CopyBufferRegion(background.resource.Get(), 0, upload_bg.Get(), 0, bytes);
                transfers.list->CopyBufferRegion(foreground.resource.Get(), 0, upload_fg.Get(), 0, bytes);
                transfers.barrier(background.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                transfers.barrier(foreground.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
                transfers.finish(); // Producer completion before NPU reads.
                stage = "npu_infer";
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
                const auto *actual = static_cast<const ov::float16 *>(mapped);
                for (size_t i = 0; i < pixels; ++i) {
                    for (size_t c = 0; c < 4; ++c) {
                        const float expected = float(f[c * pixels + i]) +
                            (1 - float(f[3 * pixels + i])) * float(b[c * pixels + i]);
                        const float error = std::abs(float(actual[c * pixels + i]) - expected);
                        if (!std::isfinite(error) || error > 2.f / 255)
                            throw std::runtime_error("shared buffer pixels differ from source-over oracle");
                        maximum_error = std::max(maximum_error, error);
                    }
                }
                D3D12_RANGE no_write{0, 0};
                readback->Unmap(0, &no_write);
                roundtrip_ms.push_back(std::chrono::duration<double, std::milli>(Clock::now() - started).count());
            }
        }
        std::cout << "{\"passed\":true,\"adapter\":" << quote(adapter_name)
                  << ",\"execution_device\":\"NPU\",\"shape\":[1,4,64,64],\"frames\":8,"
                  << "\"max_abs_error\":" << maximum_error
                  << ",\"input_and_output_shared\":true,\"host_waited_fences\":true,"
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
