#pragma once
// Shared native graphics resources. Each executable owns its renderer and window.
#include "d3d12_buffers.hpp"
#include "blend_model.hpp"
#include <d3dcompiler.h>
#include <openvino/runtime/intel_npu/properties.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <thread>

inline double milliseconds(Clock::time_point from, Clock::time_point to = Clock::now()) {
    return std::chrono::duration<double, std::milli>(to - from).count();
}

inline double process_cpu_seconds() {
    FILETIME created{}, ended{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &ended, &kernel, &user))
        throw std::runtime_error("GetProcessTimes failed");
    const uint64_t k = (uint64_t(kernel.dwHighDateTime) << 32) | kernel.dwLowDateTime;
    const uint64_t u = (uint64_t(user.dwHighDateTime) << 32) | user.dwLowDateTime;
    return double(k + u) / 1e7;
}

struct FramePacer {
    Handle timer;
    FramePacer() {
        timer.value = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!timer.value) throw std::runtime_error("high-resolution pacing timer unavailable");
    }
    void until(Clock::time_point target) {
        const auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(target - Clock::now()).count();
        if (remaining <= 0) return;
        LARGE_INTEGER due; due.QuadPart = -std::max<int64_t>(1, remaining / 100);
        if (!SetWaitableTimer(timer.value, &due, 0, nullptr, nullptr, FALSE) ||
            WaitForSingleObject(timer.value, 1000) != WAIT_OBJECT_0)
            throw std::runtime_error("frame pacing timer failed");
    }
};

struct NpuBlend {
    ov::Core core;
    ov::CompiledModel compiled;
    ov::Tensor background, foreground, output;
    ov::InferRequest request;
    std::string driver_version, driver_error;
    std::string workload;
    int64_t requested_tiles = 0, max_tiles = 0;
    NpuBlend(UINT width, UINT height, HANDLE bg, HANDLE fg, HANDLE out, const std::string &expected_driver,
             const std::string &alpha_mode, bool profile, const std::string &requested_workload = "DEFAULT",
             int64_t tiles = 0) : requested_tiles(tiles) {
        try {
            std::ostringstream version;
            core.get_property("NPU", "NPU_DRIVER_VERSION").print(version);
            driver_version = version.str();
        } catch (const std::exception &error) { driver_error = error.what(); }
        if (!expected_driver.empty() && driver_version != expected_driver)
            throw std::runtime_error("expected loaded NPU driver " + expected_driver +
                ", renderer runtime reported " + driver_version);
        ov::AnyMap settings{{"PERF_COUNT", profile}};
        max_tiles = core.get_property("NPU", ov::intel_npu::max_tiles);
        if (tiles < 0 || tiles > max_tiles) throw std::invalid_argument("NPU tile request exceeds device capabilities");
        if (tiles) settings.emplace(ov::intel_npu::tiles(tiles));
        if (requested_workload == "EFFICIENT") settings.emplace(ov::workload_type(ov::WorkloadType::EFFICIENT));
        else if (requested_workload != "DEFAULT") throw std::invalid_argument("invalid NPU workload");
        compiled = core.compile_model(blend_model(width, height, true, 4, alpha_mode), "NPU", settings);
        std::ostringstream actual_workload;
        actual_workload << compiled.get_property(ov::workload_type);
        workload = actual_workload.str();
        if (workload != requested_workload) throw std::runtime_error("NPU workload readback mismatch");
        auto devices = compiled.get_property(ov::execution_devices);
        if (devices.empty() || std::any_of(devices.begin(), devices.end(), [](const auto &d) {
            return d != "NPU" && d.rfind("NPU.", 0) != 0;
        })) throw std::runtime_error("NPU execution not confirmed");
        auto context = compiled.get_context();
        auto tensor = [&](HANDLE handle) {
            return context.create_tensor(ov::element::u8, {1, height, width, 4},
                {{"MEM_TYPE", std::string("SHARED_BUF")}, {"MEM_HANDLE", static_cast<void *>(handle)},
                 {"TENSOR_TYPE", std::string("BINDED")}});
        };
        background = tensor(bg); foreground = tensor(fg); output = tensor(out);
        request = compiled.create_infer_request();
        request.set_input_tensor(0, background); request.set_input_tensor(1, foreground);
        request.set_output_tensor(output);
    }
};

struct GpuBlend {
    ComPtr<ID3D12RootSignature> root;
    ComPtr<ID3D12PipelineState> pipeline;
    explicit GpuBlend(ID3D12Device *device) {
        const char *shader = R"(
ByteAddressBuffer background : register(t0);
ByteAddressBuffer foreground : register(t1);
RWByteAddressBuffer output : register(u0);
cbuffer Dimensions : register(b0) { uint pixels; };
[numthreads(256,1,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= pixels) return;
    uint b = background.Load(tid.x * 4), f = foreground.Load(tid.x * 4);
    uint4 shifts = uint4(0,8,16,24);
    float4 bg = (b >> shifts) & 255, fg = (f >> shifts) & 255;
    uint4 value = (uint4)round(clamp(fg + bg * (1.0 - fg.a / 255.0), 0.0, 255.0));
    output.Store(tid.x * 4, value.x | (value.y << 8) | (value.z << 16) | (value.w << 24));
})";
        ComPtr<ID3DBlob> code, errors, serialized;
        HRESULT result = D3DCompile(shader, std::strlen(shader), "npu-render-source-over", nullptr,
            nullptr, "main", "cs_5_1", D3DCOMPILE_OPTIMIZATION_LEVEL3 | D3DCOMPILE_WARNINGS_ARE_ERRORS,
            0, &code, &errors);
        if (FAILED(result)) throw std::runtime_error(errors ?
            std::string(static_cast<const char *>(errors->GetBufferPointer()), errors->GetBufferSize()) : "shader compilation failed");
        D3D12_ROOT_PARAMETER parameters[4]{};
        parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[0].Descriptor.ShaderRegister = 0;
        parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV;
        parameters[1].Descriptor.ShaderRegister = 1;
        parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV;
        parameters[2].Descriptor.ShaderRegister = 0;
        parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        parameters[3].Constants.ShaderRegister = 0;
        parameters[3].Constants.Num32BitValues = 1;
        D3D12_ROOT_SIGNATURE_DESC root_desc{};
        root_desc.NumParameters = 4; root_desc.pParameters = parameters;
        check(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &serialized, &errors), "serialize root signature");
        check(device->CreateRootSignature(0, serialized->GetBufferPointer(), serialized->GetBufferSize(),
            IID_PPV_ARGS(&root)), "create root signature");
        D3D12_COMPUTE_PIPELINE_STATE_DESC desc{};
        desc.pRootSignature = root.Get(); desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
        check(device->CreateComputePipelineState(&desc, IID_PPV_ARGS(&pipeline)), "create compute pipeline");
    }
    void dispatch(Transfers &t, SharedBuffer &bg, SharedBuffer &fg, SharedBuffer &out, UINT pixels) {
        t.begin();
        t.barrier(bg.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        t.barrier(fg.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        t.barrier(out.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        t.list->SetComputeRootSignature(root.Get()); t.list->SetPipelineState(pipeline.Get());
        t.list->SetComputeRootShaderResourceView(0, bg.resource->GetGPUVirtualAddress());
        t.list->SetComputeRootShaderResourceView(1, fg.resource->GetGPUVirtualAddress());
        t.list->SetComputeRootUnorderedAccessView(2, out.resource->GetGPUVirtualAddress());
        t.list->SetComputeRoot32BitConstant(3, pixels, 0);
        t.list->Dispatch((pixels + 255) / 256, 1, 1);
        t.barrier(bg.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        t.barrier(fg.resource.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        t.barrier(out.resource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
        t.finish();
    }
};

inline void cpu_blend(const std::vector<uint8_t> &b, const std::vector<uint8_t> &f, std::vector<uint8_t> &out) {
    for (size_t i = 0; i < b.size(); i += 4)
        for (size_t c = 0; c < 4; ++c)
            out[i + c] = uint8_t(std::min(255u, unsigned(f[i + c]) +
                (unsigned(b[i + c]) * (255u - unsigned(f[i + 3])) + 127u) / 255u));
}

// Calls complete before reusing staging storage; shared tensors outlive requests
// and die before the D3D resources, in reverse member-declaration order.
struct Renderer {
    UINT width, height, pixels;
    size_t bytes;
    ComPtr<IDXGIFactory2> factory;
    ComPtr<ID3D12Device> device;
    std::string adapter;
    std::unique_ptr<SharedBuffer> bg, fg, out;
    ComPtr<ID3D12Resource> staging_bg, staging_fg, staging_out, readback;
    std::unique_ptr<Transfers> transfers;
    std::unique_ptr<GpuBlend> gpu;
    std::unique_ptr<NpuBlend> npu;
    std::vector<uint8_t> b, f, cpu;
    unsigned max_error = 0;

    Renderer(UINT w, UINT h, const std::string &expected_driver, const std::string &alpha_mode,
             bool profile, const std::string &workload = "DEFAULT", int64_t tiles = 0)
             : width(w), height(h), pixels(w*h), bytes(size_t(w)*h*4), b(bytes), f(bytes), cpu(bytes) {
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "create DXGI factory");
        for (UINT index = 0; ; ++index) {
            ComPtr<IDXGIAdapter1> candidate;
            auto status = factory->EnumAdapters1(index, &candidate);
            if (status == DXGI_ERROR_NOT_FOUND) break;
            check(status, "enumerate adapter");
            DXGI_ADAPTER_DESC1 desc{}; check(candidate->GetDesc1(&desc), "adapter description");
            if (desc.VendorId != 0x8086 || (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)) continue;
            if (SUCCEEDED(D3D12CreateDevice(candidate.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
                char name[256]{}; WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name, sizeof(name), nullptr, nullptr);
                adapter = name; break;
            }
        }
        if (!device) throw std::runtime_error("Intel D3D12 adapter unavailable");
        bg = std::make_unique<SharedBuffer>(device.Get(), bytes);
        fg = std::make_unique<SharedBuffer>(device.Get(), bytes);
        out = std::make_unique<SharedBuffer>(device.Get(), bytes);
        staging_bg = staging(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD);
        staging_fg = staging(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD);
        staging_out = staging(device.Get(), bytes, D3D12_HEAP_TYPE_UPLOAD);
        readback = staging(device.Get(), bytes, D3D12_HEAP_TYPE_READBACK);
        transfers = std::make_unique<Transfers>(device.Get());
        gpu = std::make_unique<GpuBlend>(device.Get());
        npu = std::make_unique<NpuBlend>(w, h, bg->shared.value, fg->shared.value, out->shared.value,
            expected_driver, alpha_mode, profile, workload, tiles);
    }
    void copy_input(ID3D12Resource *staged, SharedBuffer &destination, const std::vector<uint8_t> &data) {
        upload(staged, data.data(), bytes);
        transfers->barrier(destination.resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
        transfers->list->CopyBufferRegion(destination.resource.Get(), 0, staged, 0, bytes);
        transfers->barrier(destination.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COMMON);
    }
    void inputs(bool background) {
        transfers->begin();
        if (background) copy_input(staging_bg.Get(), *bg, b);
        copy_input(staging_fg.Get(), *fg, f);
        transfers->finish();
    }
    void compose(const std::string &backend) {
        if (backend == "NPU") npu->request.infer();
        else if (backend == "GPU") gpu->dispatch(*transfers, *bg, *fg, *out, pixels);
        else if (backend == "CPU" || backend == "IDLE") {
            cpu_blend(b, f, cpu);
            transfers->begin(); copy_input(staging_out.Get(), *out, cpu); transfers->finish();
        } else throw std::invalid_argument("unknown backend");
    }
    void qualify() {
        for (unsigned fixture = 0; fixture < 3; ++fixture) {
            for (size_t i = 0; i < pixels; ++i) {
                const unsigned alpha = fixture == 0 ? 0 : fixture == 1 ? 255 : (i * 31 + 17) % 256;
                for (size_t c = 0; c < 4; ++c) {
                    b[i*4+c] = uint8_t(c == 3 ? 255 : (i * 7 + c * 23) % 256);
                    f[i*4+c] = uint8_t(c == 3 ? alpha : ((i + c * 37) % 256) * alpha / 255);
                }
            }
            inputs(true); cpu_blend(b, f, cpu);
            for (const std::string backend : {"GPU", "NPU"}) {
                compose(backend);
                transfers->begin();
                transfers->barrier(out->resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
                transfers->list->CopyBufferRegion(readback.Get(), 0, out->resource.Get(), 0, bytes);
                transfers->barrier(out->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
                transfers->finish();
                void *mapped = nullptr; D3D12_RANGE read{0, bytes};
                check(readback->Map(0, &read, &mapped), "map qualification pixels");
                unsigned maximum = 0;
                for (size_t i = 0; i < bytes; ++i) maximum = std::max(maximum,
                    unsigned(std::abs(int(static_cast<uint8_t *>(mapped)[i]) - int(cpu[i]))));
                D3D12_RANGE no_write{0,0}; readback->Unmap(0, &no_write);
                max_error = std::max(max_error, maximum);
                if (maximum > 1) throw std::runtime_error(backend + " failed full-shape byte oracle, max error=" + std::to_string(maximum));
            }
        }
    }
    void background() {
        for (UINT y = 0; y < height; ++y) for (UINT x = 0; x < width; ++x) {
            const size_t p = (size_t(y)*width+x)*4;
            b[p] = uint8_t(35 + 60*x/width); b[p+1] = uint8_t(25 + 50*y/height);
            b[p+2] = 22; b[p+3] = 255;
        }
    }
    void scene(double elapsed) {
        std::fill(f.begin(), f.end(), 0);
        for (unsigned box = 0; box < 3; ++box) {
            UINT bw = width/4, bh = height/3;
            UINT x0 = UINT((width-bw) * (0.5 + 0.45*std::sin(elapsed*(0.6+box*0.1)+box*2)));
            UINT y0 = UINT((height-bh) * (0.5 + 0.45*std::cos(elapsed*0.7+box*2)));
            uint8_t alpha = uint8_t(100+box*50);
            for (UINT y = y0; y < y0+bh; ++y) for (UINT x = x0; x < x0+bw; ++x) {
                size_t p = (size_t(y)*width+x)*4;
                f[p] = box == 0 ? alpha : alpha/4;
                f[p+1] = box == 1 ? alpha : alpha/4;
                f[p+2] = box == 2 ? alpha : alpha/4; f[p+3] = alpha;
            }
        }
    }
};

struct Window {
    HWND handle = nullptr;
    ComPtr<IDXGISwapChain3> swapchain;
    std::array<ComPtr<ID3D12Resource>, 2> buffers;
    std::string requested;
    bool stopped = false;
    bool allow_switch = true;
    std::array<std::string, 3> key_modes{"GPU", "NPU", "CPU"};
    static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
        Window *self = reinterpret_cast<Window *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            self = static_cast<Window *>(reinterpret_cast<CREATESTRUCTW *>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (self && message == WM_KEYDOWN) {
            if (self->allow_switch && wparam >= '1' && wparam <= '3')
                self->requested = self->key_modes[size_t(wparam - '1')];
            if (wparam == VK_ESCAPE) self->stopped = true;
            return 0;
        }
        if (self && message == WM_CLOSE) { self->stopped = true; return 0; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
    Window(Renderer &r) {
        SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
        SetProcessDPIAware();
        WNDCLASSW klass{}; klass.lpfnWndProc = procedure; klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = L"NpuRenderSharedBuffers"; klass.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (!RegisterClassW(&klass)) throw std::runtime_error("window class registration failed");
        RECT rect{0,0,LONG(r.width),LONG(r.height)};
        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        check(AdjustWindowRect(&rect, style, FALSE) ? S_OK : E_FAIL, "window rectangle");
        handle = CreateWindowW(klass.lpszClassName, L"npu-render", style, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right-rect.left, rect.bottom-rect.top, nullptr, nullptr, klass.hInstance, this);
        if (!handle) throw std::runtime_error("window creation failed");
        DXGI_SWAP_CHAIN_DESC1 desc{};
        desc.Width = r.width; desc.Height = r.height; desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        desc.Scaling = DXGI_SCALING_STRETCH; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
        ComPtr<IDXGISwapChain1> chain;
        check(r.factory->CreateSwapChainForHwnd(r.transfers->queue.Get(), handle, &desc, nullptr, nullptr, &chain), "create swapchain");
        check(chain.As(&swapchain), "get swapchain3");
        check(r.factory->MakeWindowAssociation(handle, DXGI_MWA_NO_ALT_ENTER), "disable fullscreen shortcut");
        for (UINT i = 0; i < 2; ++i) check(swapchain->GetBuffer(i, IID_PPV_ARGS(&buffers[i])), "get swapchain buffer");
        ShowWindow(handle, SW_SHOW);
    }
    ~Window() {
        if (handle) DestroyWindow(handle);
        SetThreadExecutionState(ES_CONTINUOUS);
    }
    void messages() {
        MSG message;
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) stopped = true;
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    HRESULT present(Renderer &r) {
        auto destination = buffers[swapchain->GetCurrentBackBufferIndex()].Get();
        auto &t = *r.transfers;
        t.begin();
        t.barrier(r.out->resource.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
        t.barrier(destination, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION source{}, target{};
        source.pResource = r.out->resource.Get(); source.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        source.PlacedFootprint.Footprint = {DXGI_FORMAT_B8G8R8A8_UNORM, r.width, r.height, 1, r.width*4};
        target.pResource = destination; target.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        t.list->CopyTextureRegion(&target, 0, 0, 0, &source, nullptr);
        t.barrier(r.out->resource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        t.barrier(destination, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT);
        t.finish();
        const HRESULT status = swapchain->Present(0, 0);
        check(status, "Present");
        return status;
    }
    void title(const std::string &backend) {
        SetWindowTextA(handle, ("npu-render | " + backend +
            (allow_switch ? " | 1 GPU / 2 NPU / 3 CPU / Esc exit" : " | diagnostic control | Esc exit")).c_str());
    }
};
