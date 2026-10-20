// Native graphics producer -> separate NPU worker -> native shader consumer.
// No driver registration, DWM routing, hidden backend or desktop-offload claim.
#include "../windows/typed_texture.hpp"
#include <array>
#include <thread>

namespace {
constexpr UINT frame_count = 32;
struct Shape {
    UINT width = 64, height = 48;
    DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
    size_t bytes() const { return size_t(width) * height * 4; }
    void validate() const {
        if (!width || width > 4096 || width % 64 || !height || height > 4096 ||
            (format != DXGI_FORMAT_B8G8R8A8_UNORM && format != DXGI_FORMAT_R8G8B8A8_UNORM))
            throw std::runtime_error("unsupported texture shape or format");
    }
};
struct Graphics {
    ov::Core core;
    Diagnostics diagnostics;
    ComPtr<ID3D12Device> twelve;
    ComPtr<ID3D12CompatibilityDevice> compatibility;
    ComPtr<ID3D11Device> eleven;
    ComPtr<ID3D11Device1> eleven1;
    ComPtr<ID3D11DeviceContext> context;
    void create(bool debug) {
        if (debug) {
            ComPtr<ID3D12Debug> layer;
            check(D3D12GetDebugInterface(IID_PPV_ARGS(&layer)), "D3D12 debug layer");
            layer->EnableDebugLayer();
        }
        const auto id = core.get_property("GPU", ov::device::luid);
        LUID luid{};
        std::memcpy(&luid, id.luid.data(), sizeof(luid));
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter1> adapter;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "DXGI factory");
        check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "exact graphics adapter");
        check(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&twelve)),
              "D3D12 device");
        D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
        check(twelve->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options)),
              "graphics capabilities");
        if (!options.CrossAdapterRowMajorTextureSupported)
            throw std::runtime_error("cross-adapter row-major textures unsupported");
        check(twelve.As(&compatibility), "compatibility interface");
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr,
                               debug ? D3D11_CREATE_DEVICE_DEBUG : 0, &level, 1,
                               D3D11_SDK_VERSION, &eleven, nullptr, &context),
              "D3D11 device");
        check(eleven.As(&eleven1), "D3D11.1 interface");
        if (debug) {
            check(eleven.As(&diagnostics.eleven), "D3D11 info queue");
            check(twelve.As(&diagnostics.twelve), "D3D12 info queue");
        }
    }
    void finish() {
        D3D11_QUERY_DESC desc{D3D11_QUERY_EVENT, 0};
        ComPtr<ID3D11Query> query;
        check(eleven->CreateQuery(&desc, &query), "GPU completion query");
        context->End(query.Get());
        context->Flush();
        const auto deadline = Clock::now() + std::chrono::seconds(10);
        for (;;) {
            BOOL done = FALSE;
            const auto status = context->GetData(query.Get(), &done, sizeof(done),
                                                 D3D11_ASYNC_GETDATA_DONOTFLUSH);
            check(status, "GPU completion");
            if (status == S_OK && done)
                return;
            if (Clock::now() > deadline)
                throw std::runtime_error("GPU completion deadline expired");
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    void validate() {
        diagnostics.dump();
        if (diagnostics.errors)
            throw std::runtime_error("graphics debug queue reported errors");
    }
};
struct Surface {
    Handle shared;
    ComPtr<ID3D12Resource> twelve;
    ComPtr<ID3D11Texture2D> eleven;
    ComPtr<ID3D11RenderTargetView> target;
    UINT64 allocation_bytes = 0;
    void open(Graphics &gpu, HANDLE handle, const Shape &shape) {
        check(gpu.twelve->OpenSharedHandle(handle, IID_PPV_ARGS(&twelve)), "open typed texture");
        const auto desc = twelve->GetDesc();
        if (desc.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || desc.Width != shape.width ||
            desc.Height != shape.height || desc.Format != shape.format || desc.MipLevels != 1 ||
            desc.DepthOrArraySize != 1 || desc.SampleDesc.Count != 1 ||
            desc.Layout != D3D12_TEXTURE_LAYOUT_ROW_MAJOR ||
            !(desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER))
            throw std::runtime_error("imported texture metadata mismatch");
        check(gpu.eleven1->OpenSharedResource1(handle, IID_PPV_ARGS(&eleven)),
              "native D3D11 NT texture open");
        check(gpu.eleven->CreateRenderTargetView(eleven.Get(), nullptr, &target), "texture RTV");
        allocation_bytes = gpu.twelve->GetResourceAllocationInfo(0, 1, &desc).SizeInBytes;
    }
    void create(Graphics &gpu, const Shape &shape) {
        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = shape.width;
        desc.Height = shape.height;
        desc.Format = shape.format;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER |
                     D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS |
                     D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        D3D11_RESOURCE_FLAGS flags{};
        flags.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        flags.MiscFlags = D3D11_RESOURCE_MISC_SHARED | D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
        ComPtr<ID3D12Resource> owner;
        check(gpu.compatibility->CreateSharedResource(
                  &heap, D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER, &desc,
                  D3D12_RESOURCE_STATE_COMMON, nullptr, &flags,
                  D3D12_COMPATIBILITY_SHARED_FLAG_NONE, nullptr, nullptr, IID_PPV_ARGS(&owner)),
              "create graphics-owned texture");
        check(gpu.twelve->CreateSharedHandle(owner.Get(), nullptr, GENERIC_ALL, nullptr,
                                           &shared.value), "share typed texture");
        open(gpu, shared.value, shape);
    }
    void release_creator() {
        target.Reset();
        eleven.Reset();
        twelve.Reset();
        if (shared.value && !CloseHandle(shared.value))
            throw std::runtime_error("close creator handle failed");
        shared.value = nullptr;
    }
};
struct Producer {
    ShaderConsumer pipeline;
    ComPtr<ID3D11Buffer> constants;
    Shape shape;
    void create(Graphics &gpu, Surface &surface, const Shape &s) {
        shape = s;
        pipeline.create(gpu.eleven.Get(), surface.eleven.Get());
        constexpr char shader[] = R"(
cbuffer Frame : register(b0) { uint width; uint frame; uint foreground; uint bgra; };
float4 main(float4 position : SV_POSITION) : SV_TARGET {
    uint i = uint(position.y) * width + uint(position.x);
    uint a = (i * 19 + frame * 31) % 256;
    uint4 color;
    [unroll] for (uint c = 0; c < 4; ++c) {
        color[c] = foreground ? (c == 3 ? a : ((i + c*23 + frame*7) % 256)*a/255)
                              : (c == 3 ? 255 : (i + c*37 + frame*17) % 256);
    }
    if (bgra) color = color.zyxw;
    return float4(color) / 255.0;
})";
        ComPtr<ID3DBlob> code, errors;
        check(D3DCompile(shader, sizeof(shader) - 1, nullptr, nullptr, nullptr, "main", "ps_5_0",
                         D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &errors), "producer shader");
        pipeline.ps.Reset();
        check(gpu.eleven->CreatePixelShader(code->GetBufferPointer(), code->GetBufferSize(),
                                           nullptr, &pipeline.ps), "producer pixel shader");
        D3D11_BUFFER_DESC desc{};
        desc.ByteWidth = 16;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        check(gpu.eleven->CreateBuffer(&desc, nullptr, &constants), "producer constants");
        // No SRV binding: the producer generates pixels in its graphics shader.
        pipeline.srv.Reset();
        pipeline.target.Reset();
        pipeline.rtv.Reset();
    }
    void draw(Graphics &gpu, Surface &surface, UINT frame, bool foreground) {
        const UINT values[]{shape.width, frame, foreground ? 1u : 0u,
                            shape.format == DXGI_FORMAT_B8G8R8A8_UNORM ? 1u : 0u};
        gpu.context->UpdateSubresource(constants.Get(), 0, nullptr, values, 0, 0);
        auto *buffer = constants.Get();
        gpu.context->PSSetConstantBuffers(0, 1, &buffer);
        pipeline.rtv = surface.target;
        pipeline.draw(gpu.context.Get());
        pipeline.rtv.Reset();
        buffer = nullptr;
        gpu.context->PSSetConstantBuffers(0, 1, &buffer);
    }
};
struct Worker {
    Handle job, process, thread, start, done, log_read, log_write;
    std::array<HANDLE, 3> child_resources{};
    DWORD pid = 0;
    Worker() = default;
    Worker(const Worker &) = delete;
    Worker &operator=(const Worker &) = delete;
    ~Worker() {
        if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 3);
            WaitForSingleObject(process.value, 5000);
        }
        drain();
    }
    void drain() noexcept {
        DWORD available = 0, read = 0;
        while (log_read.value && PeekNamedPipe(log_read.value, nullptr, 0, nullptr, &available,
                                               nullptr) && available) {
            char buffer[4096];
            if (!ReadFile(log_read.value, buffer, std::min<DWORD>(available, sizeof(buffer)),
                          &read, nullptr) || !read)
                break;
            std::cout.write(buffer, read).flush();
        }
    }
    void launch(const std::array<Surface, 3> &surfaces, Shape shape, bool debug,
                bool reject_shape) {
        job.value = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits)))
            throw std::runtime_error("worker job creation failed");
        start.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        done.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!start.value || !done.value ||
            !CreatePipe(&log_read.value, &log_write.value, nullptr, 65536))
            throw std::runtime_error("worker IPC creation failed");
        std::array<HANDLE, 6> handles{surfaces[0].shared.value, surfaces[1].shared.value,
                                      surfaces[2].shared.value, start.value, done.value,
                                      log_write.value};
        struct Inheritance {
            std::array<HANDLE, 6> handles;
            ~Inheritance() {
                for (auto h : handles)
                    SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
            }
        } inheritance{handles};
        for (auto h : handles)
            if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
                throw std::runtime_error("worker handle inheritance failed");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<uint8_t> storage(bytes);
        auto attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attrs, 1, 0, &bytes))
            throw std::runtime_error("worker attributes failed");
        struct AttrCleanup {
            LPPROC_THREAD_ATTRIBUTE_LIST p;
            ~AttrCleanup() { DeleteProcThreadAttributeList(p); }
        } cleanup{attrs};
        if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles.data(), sizeof(handles), nullptr, nullptr))
            throw std::runtime_error("worker handle whitelist failed");
        wchar_t path[32768]{};
        const auto count = GetModuleFileNameW(nullptr, path, 32768);
        if (!count || count == 32768)
            throw std::runtime_error("worker executable unavailable");
        std::wstring command = L"\"" + std::wstring(path) + L"\" --worker";
        for (size_t i = 0; i < 5; ++i)
            command += L" " + std::to_wstring(uintptr_t(handles[i]));
        command += L" " + std::to_wstring(shape.width + (reject_shape ? 64 : 0)) + L" " +
                   std::to_wstring(shape.height) + L" " + std::to_wstring(UINT(shape.format)) +
                   L" " + std::to_wstring(debug ? 1 : 0);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log_write.value;
        startup.lpAttributeList = attrs;
        PROCESS_INFORMATION info{};
        if (!CreateProcessW(path, command.data(), nullptr, nullptr, TRUE,
                            EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
                            nullptr, nullptr, &startup.StartupInfo, &info))
            throw std::runtime_error("worker launch failed");
        process.value = info.hProcess;
        thread.value = info.hThread;
        pid = info.dwProcessId;
        if (!AssignProcessToJobObject(job.value, process.value))
            throw std::runtime_error("worker job assignment failed");
        if (ResumeThread(thread.value) == DWORD(-1))
            throw std::runtime_error("worker resume failed");
        child_resources = {handles[0], handles[1], handles[2]};
        std::cout << "{\"worker_pid\":" << pid << ",\"parent_pid\":" << GetCurrentProcessId()
                  << ",\"worker_job_kill_on_close\":true,\"inherited_handles\":6}\n" << std::flush;
        wait(60000);
    }
    void wait(DWORD milliseconds) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
        const HANDLE handles[]{done.value, process.value};
        while (Clock::now() < deadline) {
            drain();
            const DWORD result = WaitForMultipleObjects(2, handles, FALSE, 100);
            if (result == WAIT_OBJECT_0) {
                drain();
                return;
            }
            if (result != WAIT_TIMEOUT) {
                drain();
                throw std::runtime_error("NPU worker exited before completion");
            }
        }
        throw std::runtime_error("NPU worker deadline expired");
    }
    void infer() {
        if (!SetEvent(start.value))
            throw std::runtime_error("worker dispatch failed");
        wait(15000);
    }
    void finish() {
        const auto deadline = Clock::now() + std::chrono::seconds(15);
        while (WaitForSingleObject(process.value, 50) == WAIT_TIMEOUT) {
            drain();
            if (Clock::now() >= deadline)
                throw std::runtime_error("worker cleanup deadline expired");
        }
        drain();
        DWORD exit = 0;
        if (!GetExitCodeProcess(process.value, &exit) || exit)
            throw std::runtime_error("worker failed at shutdown");
    }
    void reopen(std::array<Surface, 3> &surfaces, Graphics &gpu, const Shape &shape) {
        gpu.context->ClearState();
        gpu.finish();
        for (auto &s : surfaces)
            s.release_creator();
        for (size_t i = 0; i < surfaces.size(); ++i) {
            if (!DuplicateHandle(process.value, child_resources[i], GetCurrentProcess(),
                                 &surfaces[i].shared.value, 0, FALSE, DUPLICATE_SAME_ACCESS))
                throw std::runtime_error("reopen child-owned texture handle failed");
            surfaces[i].open(gpu, surfaces[i].shared.value, shape);
        }
        std::cout << "{\"creator_released_and_reopened\":3}\n" << std::flush;
    }
};

int child(int argc, char **argv) {
    if (argc != 11)
        throw std::runtime_error("invalid worker arguments");
    const Shape shape{UINT(std::stoul(argv[7])), UINT(std::stoul(argv[8])),
                      DXGI_FORMAT(std::stoul(argv[9]))};
    shape.validate();
    std::array<Handle, 5> handles;
    for (size_t i = 0; i < handles.size(); ++i) {
        handles[i].value = reinterpret_cast<HANDLE>(uintptr_t(std::stoull(argv[i + 2])));
        DWORD flags = 0;
        if (!handles[i].value || !GetHandleInformation(handles[i].value, &flags))
            throw std::runtime_error("invalid inherited handle");
        SetHandleInformation(handles[i].value, HANDLE_FLAG_INHERIT, 0);
    }
    Graphics gpu;
    gpu.create(std::string(argv[10]) == "1");
    std::array<Surface, 3> surfaces;
    std::array<TextureAlias, 3> aliases;
    for (size_t i = 0; i < surfaces.size(); ++i) {
        surfaces[i].open(gpu, handles[i].value, shape);
        aliases[i].create(gpu.core, handles[i].value, surfaces[i].allocation_bytes, true, false);
    }
    gpu.validate();
    {
        auto model = gpu.core.compile_model(
            blend_model(shape.width, shape.height, true, 4, "convolution"), "NPU",
            ov::hint::performance_mode(ov::hint::PerformanceMode::LATENCY));
        const auto executions = model.get_property(ov::execution_devices);
        if (executions.size() != 1 ||
            (executions[0] != "NPU" && executions[0].rfind("NPU.", 0) != 0))
            throw std::runtime_error("worker execution identity mismatch");
        auto remote = model.get_context();
        std::array<ov::Tensor, 3> tensors;
        for (size_t i = 0; i < tensors.size(); ++i)
            tensors[i] = remote.create_tensor(
                ov::element::u8, {1, shape.height, shape.width, 4},
                {{"MEM_TYPE", std::string("SHARED_BUF")},
                 {"MEM_HANDLE", static_cast<void *>(aliases[i].shared)},
                 {"TENSOR_TYPE", std::string("BINDED")}});
        auto request = model.create_infer_request();
        request.set_input_tensor(0, tensors[0]);
        request.set_input_tensor(1, tensors[1]);
        request.set_output_tensor(tensors[2]);
        std::cout << "{\"worker_ready\":true,\"execution_device\":" << quote(executions[0])
                  << ",\"npu_driver\":"
                  << gpu.core.get_property("NPU", ov::intel_npu::driver_version)
                  << ",\"host_pixel_upload_bytes\":0,\"host_output_copy_bytes\":0}\n" << std::flush;
        if (!SetEvent(handles[4].value))
            throw std::runtime_error("worker ready event failed");
        for (UINT frame = 0; frame < frame_count; ++frame) {
            if (WaitForSingleObject(handles[3].value, 30000) != WAIT_OBJECT_0)
                throw std::runtime_error("worker producer deadline expired");
            const auto started = Clock::now();
            request.infer();
            std::cout << "{\"worker_frame\":" << frame << ",\"npu_ms\":"
                      << std::chrono::duration<double, std::milli>(Clock::now() - started).count()
                      << "}\n" << std::flush;
            if (!SetEvent(handles[4].value))
                throw std::runtime_error("worker completion failed");
        }
    }
    unsigned cleanup_errors = 0;
    for (auto &a : aliases) {
        a.close();
        cleanup_errors += a.cleanup_failures;
    }
    gpu.validate();
    if (cleanup_errors)
        throw std::runtime_error("worker texture cleanup failed");
    std::cout << "{\"worker_passed\":true,\"worker_frames\":" << frame_count
              << ",\"worker_graphics_debug_errors\":" << gpu.diagnostics.errors << "}\n"
              << std::flush;
    return 0;
}
unsigned pixel_error(const Shape &shape, UINT frame, const D3D11_MAPPED_SUBRESOURCE &map) {
    unsigned error = 0;
    for (UINT y = 0; y < shape.height; ++y)
        for (UINT x = 0; x < shape.width; ++x) {
            const UINT i = y * shape.width + x, alpha = (i * 19 + frame * 31) % 256;
            for (UINT c = 0; c < 4; ++c) {
                const UINT bg = c == 3 ? 255 : (i + c * 37 + frame * 17) % 256;
                const UINT fg = c == 3 ? alpha : ((i + c * 23 + frame * 7) % 256) * alpha / 255;
                const auto expected = std::min(255u, fg + (bg * (255 - alpha) + 127) / 255);
                const auto observed = static_cast<const uint8_t *>(map.pData)[
                    size_t(y) * map.RowPitch + x * 4 + c];
                error = std::max(error, unsigned(std::abs(int(expected) - int(observed))));
            }
        }
    return error;
}
} // namespace

int main(int argc, char **argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    const bool worker = argc > 1 && std::string(argv[1]) == "--worker";
    try {
        if (worker)
            return child(argc, argv);
        Shape shape;
        bool debug = false, reopen = false, reject_shape = false;
        for (int i = 1; i < argc; ++i) {
            const std::string option(argv[i]);
            if (option == "--debug")
                debug = true;
            else if (option == "--reopen")
                reopen = true;
            else if (option == "--reject-worker-shape")
                reject_shape = true;
            else if (option == "--rgba")
                shape.format = DXGI_FORMAT_R8G8B8A8_UNORM;
            else if ((option == "--width" || option == "--height") && i + 1 < argc) {
                const std::string value(argv[++i]);
                if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
                    throw std::runtime_error("invalid dimension");
                const auto n = std::stoull(value);
                if (!n || n > 4096)
                    throw std::runtime_error("dimension outside 1..4096");
                (option == "--width" ? shape.width : shape.height) = UINT(n);
            } else
                throw std::runtime_error("use [--debug] [--reopen] [--rgba] --width N --height N");
        }
        shape.validate();
        Graphics gpu;
        gpu.create(debug);
        std::array<Surface, 3> surfaces;
        for (auto &s : surfaces)
            s.create(gpu, shape);
        Producer producer;
        producer.create(gpu, surfaces[0], shape);
        ShaderConsumer consumer;
        consumer.create(gpu.eleven.Get(), surfaces[2].eleven.Get());
        D3D11_TEXTURE2D_DESC desc{};
        surfaces[2].eleven->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = desc.MiscFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        ComPtr<ID3D11Texture2D> readback;
        check(gpu.eleven->CreateTexture2D(&desc, nullptr, &readback), "consumer readback");
        Worker child;
        child.launch(surfaces, shape, debug, reject_shape);
        unsigned maximum_error = 0;
        for (UINT frame = 0; frame < frame_count; ++frame) {
            if (reopen && frame == frame_count / 2) {
                consumer.srv.Reset();
                child.reopen(surfaces, gpu, shape);
                check(gpu.eleven->CreateShaderResourceView(surfaces[2].eleven.Get(), nullptr,
                                                          &consumer.srv), "reopened SRV");
            }
            producer.draw(gpu, surfaces[0], frame, false);
            producer.draw(gpu, surfaces[1], frame, true);
            gpu.finish(); // GPU producer completes before the child reads the shared pages.
            child.infer(); // NPU completes before the GPU samples the result.
            consumer.draw(gpu.context.Get());
            gpu.context->CopyResource(readback.Get(), consumer.target.Get());
            D3D11_MAPPED_SUBRESOURCE map{};
            check(gpu.context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &map), "output Map");
            const unsigned error = pixel_error(shape, frame, map);
            gpu.context->Unmap(readback.Get(), 0);
            maximum_error = std::max(maximum_error, error);
            std::cout << "{\"frame\":" << frame << ",\"max_byte_error\":" << error << "}\n"
                      << std::flush;
            if (error > 1)
                throw std::runtime_error("cross-process texture pixel oracle failed");
            gpu.validate();
        }
        child.finish();
        gpu.context->ClearState();
        gpu.finish();
        gpu.validate();
        std::cout << "{\"passed\":true,\"full_desktop_offload\":false,\"cross_process\":true,"
                     "\"native_texture_inputs\":2,\"native_texture_outputs\":1,"
                     "\"host_pixel_upload_bytes\":0,\"host_output_copy_bytes\":0,"
                     "\"host_synchronized\":true,\"gpu_producer_draws\":" << 2 * frame_count
                  << ",\"gpu_consumer_draws\":" << frame_count
                  << ",\"host_constant_upload_bytes\":" << 32 * frame_count
                  << ",\"frames\":" << frame_count << ",\"width\":" << shape.width
                  << ",\"height\":" << shape.height << ",\"format\":"
                  << quote(shape.format == DXGI_FORMAT_B8G8R8A8_UNORM ? "BGRA" : "RGBA")
                  << ",\"max_byte_error\":" << maximum_error
                  << ",\"creator_reopened\":" << (reopen ? "true" : "false")
                  << ",\"graphics_debug_enabled\":" << (debug ? "true" : "false")
                  << ",\"graphics_debug_errors\":" << gpu.diagnostics.errors << "}\n";
        return 0;
    } catch (const std::exception &e) {
        std::cout << "{\"passed\":false,\"full_desktop_offload\":false,\"worker\":"
                  << (worker ? "true" : "false") << ",\"error\":" << quote(e.what()) << "}\n"
                  << std::flush;
        return 1;
    }
}
