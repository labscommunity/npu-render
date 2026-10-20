#define NOMINMAX
#include "contract.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <d3d11.h>
#include <d3d11sdklayers.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <io.h>
#include <iostream>
#include <openvino/openvino.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
static void check(HRESULT hr, const char *where) {
    if (FAILED(hr)) {
        std::ostringstream s;
        s << where << " HRESULT=0x" << std::hex << uint32_t(hr);
        throw std::runtime_error(s.str());
    }
}
static std::string quote(const std::string &s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            r += '\\';
            r += char(c);
        } else if (c == '\n')
            r += "\\n";
        else if (c == '\r')
            r += "\\r";
        else if (c >= 32)
            r += char(c);
    }
    return r + '"';
}
static ComPtr<ID3DBlob> compile(const char *source, const char *profile) {
    ComPtr<ID3DBlob> b, e;
    check(D3DCompile(source, std::strlen(source), nullptr, nullptr, nullptr, "main", profile,
                     D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &b, &e),
          "D3DCompile");
    return b;
}

struct Handle {
    HANDLE value = nullptr;
    ~Handle() {
        if (value)
            CloseHandle(value);
    }
    Handle() = default;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
};
UINT width = 64, height = 48, frames = 16;
DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM;
void dimensions(wchar_t **args) {
    auto number = [](const wchar_t *text) {
        const std::wstring value(text);
        if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
            throw std::runtime_error("invalid unsigned dimension");
        const auto n = std::stoull(value);
        if (n > 4096)
            throw std::runtime_error("dimension exceeds experiment bounds");
        return UINT(n);
    };
    width = number(args[0]);
    height = number(args[1]);
    frames = number(args[2]);
    if (!width || width % 32 || !height || !frames || frames > 256)
        throw std::runtime_error("dimensions or frame count outside experiment bounds");
    const std::wstring name(args[3]);
    if (name != L"BGRA" && name != L"RGBA")
        throw std::runtime_error("invalid texture format");
    format = name == L"BGRA" ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
}
struct Session {
    HMODULE module = nullptr;
    NrDdiGetStats get = nullptr;
    NrDdiGetSharedStats get_shared = nullptr;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ~Session() {
        if (context)
            context->ClearState();
        context.Reset();
        device.Reset();
        if (module)
            FreeLibrary(module);
    }
    void create(const std::wstring &path) {
        module = LoadLibraryExW(path.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!module)
            throw std::runtime_error("explicit shared DLL load failed");
        auto configure =
            reinterpret_cast<NrDdiConfigure>(GetProcAddress(module, "nr_ddi_configure"));
        get = reinterpret_cast<NrDdiGetStats>(GetProcAddress(module, "nr_ddi_get_stats"));
        get_shared = reinterpret_cast<NrDdiGetSharedStats>(
            GetProcAddress(module, "nr_ddi_get_shared_stats"));
        if (!configure || !get || !get_shared || !configure("NPU"))
            throw std::runtime_error("NPU configuration failed");
        const D3D_FEATURE_LEVEL requested = D3D_FEATURE_LEVEL_10_0;
        D3D_FEATURE_LEVEL actual{};
        check(
            D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_SOFTWARE, module,
                              D3D11_CREATE_DEVICE_SINGLETHREADED | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                              &requested, 1, D3D11_SDK_VERSION, &device, &actual, &context),
            "create shared DDI device");
        if (actual != requested)
            throw std::runtime_error("unexpected DDI ABI");
    }
    void stats(const char *role) {
        NrDdiStats s;
        get(&s);
        std::cout << "{\"role\":" << quote(role) << ",\"device\":" << quote(s.device)
                  << ",\"driver\":" << quote(s.driver) << ",\"draws\":" << s.draws
                  << ",\"executions\":" << s.executions << ",\"rejected\":" << s.rejected
                  << ",\"host_upload_bytes\":" << s.host_upload_bytes
                  << ",\"host_copy_bytes\":" << s.host_copy_bytes
                  << ",\"inference_input_bytes\":" << s.inference_input_bytes
                  << ",\"inference_output_bytes\":" << s.inference_output_bytes
                  << ",\"last_error\":" << quote(s.last_error) << "}\n"
                  << std::flush;
    }
    void check_released(const char *role) {
        context->ClearState();
        context->Flush(); // Force Direct3D's deferred object destruction before checking.
        NrDdiSharedStats s;
        if (!get_shared(&s))
            throw std::runtime_error("shared statistics ABI mismatch");
        std::cout << "{\"resource_role\":" << quote(role) << ",\"created\":" << s.created
                  << ",\"opened\":" << s.opened << ",\"live_surfaces\":" << s.live_surfaces
                  << ",\"aliases\":" << s.aliases << ",\"live_aliases\":" << s.live_aliases
                  << ",\"cleanup_failures\":" << s.cleanup_failures << "}\n"
                  << std::flush;
        if (s.live_surfaces || s.live_aliases || s.cleanup_failures)
            throw std::runtime_error("shared resource cleanup failed");
    }
};
D3D11_TEXTURE2D_DESC description(bool shared) {
    D3D11_TEXTURE2D_DESC d{};
    d.Width = width;
    d.Height = height;
    d.MipLevels = d.ArraySize = d.SampleDesc.Count = 1;
    d.Format = format;
    d.Usage = shared ? D3D11_USAGE_DEFAULT : D3D11_USAGE_STAGING;
    d.BindFlags = shared ? D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET : 0;
    d.MiscFlags = shared ? D3D11_RESOURCE_MISC_SHARED : 0;
    d.CPUAccessFlags = shared ? 0 : D3D11_CPU_ACCESS_READ;
    return d;
}
struct Worker {
    Handle process, thread, start, done, log_read, log_write, job;
    ~Worker() {
        if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 2);
            WaitForSingleObject(process.value, 5000);
        }
    }
    void launch(const std::array<HANDLE, 2> &resources, const std::wstring &dll) {
        job.value = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!job.value || !SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits)))
            throw std::runtime_error("worker lifetime job creation failed");
        start.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        done.value = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!start.value || !done.value)
            throw std::runtime_error("worker event creation failed");
        if (!CreatePipe(&log_read.value, &log_write.value, nullptr, 0))
            throw std::runtime_error("worker log pipe failed");
        std::array<HANDLE, 3> handles{start.value, done.value, log_write.value};
        for (auto h : handles)
            if (!SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
                throw std::runtime_error("setting worker handle inheritance failed");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<uint8_t> storage(bytes);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes))
            throw std::runtime_error("initializing worker attributes failed");
        struct Cleanup {
            LPPROC_THREAD_ATTRIBUTE_LIST p;
            ~Cleanup() { DeleteProcThreadAttributeList(p); }
        } cleanup{attributes};
        if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       handles.data(), sizeof(handles), nullptr, nullptr))
            throw std::runtime_error("worker handle whitelist failed");
        wchar_t filename[32768]{};
        const auto count = GetModuleFileNameW(nullptr, filename, 32768);
        if (!count || count == 32768)
            throw std::runtime_error("worker executable path unavailable");
        std::wstring command = L"\"" + std::wstring(filename) + L"\" --worker";
        command += L" \"" + dll + L"\"";
        for (auto h : resources)
            command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(h));
        for (size_t i = 0; i < 2; ++i)
            command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(handles[i]));
        command += L" " + std::to_wstring(width) + L" " + std::to_wstring(height) + L" " +
                   std::to_wstring(frames);
        command += format == DXGI_FORMAT_B8G8R8A8_UNORM ? L" BGRA" : L" RGBA";
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdOutput = startup.StartupInfo.hStdError = log_write.value;
        startup.lpAttributeList = attributes;
        PROCESS_INFORMATION child{};
        const BOOL created =
            CreateProcessW(filename, command.data(), nullptr, nullptr, TRUE,
                           EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW | CREATE_SUSPENDED,
                           nullptr, nullptr, &startup.StartupInfo, &child);
        const DWORD error = GetLastError();
        for (auto h : handles)
            SetHandleInformation(h, HANDLE_FLAG_INHERIT, 0);
        if (!created)
            throw std::runtime_error("worker launch failed: " + std::to_string(error));
        process.value = child.hProcess;
        thread.value = child.hThread;
        if (!AssignProcessToJobObject(job.value, process.value) ||
            ResumeThread(thread.value) == DWORD(-1))
            throw std::runtime_error("worker lifetime job assignment failed");
        wait(60000);
    }
    void wait(DWORD milliseconds) {
        HANDLE handles[]{done.value, process.value};
        const auto deadline = GetTickCount64() + milliseconds;
        while (GetTickCount64() < deadline) {
            const DWORD result = WaitForMultipleObjects(2, handles, FALSE, 100);
            drain_log();
            if (result == WAIT_OBJECT_0)
                return;
            if (result != WAIT_TIMEOUT)
                break;
        }
        DWORD code = 0;
        GetExitCodeProcess(process.value, &code);
        throw std::runtime_error("shared-texture worker failed or timed out, exit=" +
                                 std::to_string(code));
    }
    void drain_log() {
        DWORD available = 0, read = 0;
        if (PeekNamedPipe(log_read.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            std::vector<char> data(std::min<DWORD>(available, 16384));
            if (ReadFile(log_read.value, data.data(), DWORD(data.size()), &read, nullptr))
                std::cout.write(data.data(), read).flush();
        }
    }
    void infer() {
        if (!SetEvent(start.value))
            throw std::runtime_error("worker dispatch failed");
        wait(10000);
    }
    void finish() {
        const auto deadline = GetTickCount64() + 10000;
        DWORD result = WAIT_TIMEOUT;
        while (GetTickCount64() < deadline && result == WAIT_TIMEOUT) {
            result = WaitForSingleObject(process.value, 100);
            drain_log();
        }
        DWORD code = 0;
        if (result != WAIT_OBJECT_0 || !GetExitCodeProcess(process.value, &code) || code != 0)
            throw std::runtime_error("shared-texture worker did not shut down successfully, exit=" +
                                     std::to_string(code));
    }
};

int child_main(wchar_t **argv) {
    Session s;
    try {
        dimensions(argv + 7);
        s.create(argv[2]);
        auto &device = s.device;
        auto &context = s.context;
        std::array<HANDLE, 4> h{};
        for (size_t i = 0; i < h.size(); ++i)
            h[i] = reinterpret_cast<HANDLE>(uintptr_t(std::stoull(argv[i + 3])));
        ComPtr<ID3D11Texture2D> source, target;
        check(device->OpenSharedResource(h[0], IID_PPV_ARGS(&source)),
              "open source shared texture");
        check(device->OpenSharedResource(h[1], IID_PPV_ARGS(&target)),
              "open target shared texture");
        for (auto *t : {source.Get(), target.Get()}) {
            D3D11_TEXTURE2D_DESC d{};
            t->GetDesc(&d);
            if (d.Width != width || d.Height != height || d.Format != format || d.ArraySize != 1 ||
                d.MipLevels != 1 || d.SampleDesc.Count != 1)
                throw std::runtime_error("opened texture description mismatch");
        }
        auto vs_code = compile(nr_ddi_vertex_shader, "vs_4_0"),
             ps_code = compile(nr_ddi_pixel_shader, "ps_4_0");
        ComPtr<ID3D11VertexShader> vs;
        ComPtr<ID3D11PixelShader> ps;
        check(device->CreateVertexShader(vs_code->GetBufferPointer(), vs_code->GetBufferSize(),
                                         nullptr, &vs),
              "CreateVertexShader");
        check(device->CreatePixelShader(ps_code->GetBufferPointer(), ps_code->GetBufferSize(),
                                        nullptr, &ps),
              "CreatePixelShader");
        D3D11_BLEND_DESC bd{};
        auto &rt = bd.RenderTarget[0];
        rt.BlendEnable = TRUE;
        rt.SrcBlend = D3D11_BLEND_ONE;
        rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOp = D3D11_BLEND_OP_ADD;
        rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
        rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = 15;
        ComPtr<ID3D11BlendState> blend;
        check(device->CreateBlendState(&bd, &blend), "CreateBlendState");
        D3D11_DEPTH_STENCIL_DESC dd{};
        dd.DepthEnable = FALSE;
        dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
        ComPtr<ID3D11DepthStencilState> depth;
        check(device->CreateDepthStencilState(&dd, &depth), "CreateDepthStencilState");
        D3D11_RASTERIZER_DESC rd{};
        rd.FillMode = D3D11_FILL_SOLID;
        rd.CullMode = D3D11_CULL_NONE;
        rd.DepthClipEnable = TRUE;
        ComPtr<ID3D11RasterizerState> raster;
        check(device->CreateRasterizerState(&rd, &raster), "CreateRasterizerState");

        ComPtr<ID3D11RenderTargetView> tv;
        ComPtr<ID3D11ShaderResourceView> sv;
        check(device->CreateRenderTargetView(target.Get(), nullptr, &tv), "shared RTV");
        check(device->CreateShaderResourceView(source.Get(), nullptr, &sv), "shared SRV");
        auto *t = tv.Get();
        auto *v = sv.Get();
        context->OMSetRenderTargets(1, &t, nullptr);
        context->PSSetShaderResources(0, 1, &v);
        context->VSSetShader(vs.Get(), nullptr, 0);
        context->PSSetShader(ps.Get(), nullptr, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->OMSetBlendState(blend.Get(), nullptr, ~0u);
        context->OMSetDepthStencilState(depth.Get(), 0);
        context->RSSetState(raster.Get());
        D3D11_VIEWPORT viewport{0, 0, FLOAT(width), FLOAT(height), 0, 1};
        context->RSSetViewports(1, &viewport);
        if (!SetEvent(h[3]))
            throw std::runtime_error("worker ready event failed");
        for (UINT frame = 0; frame < frames; ++frame) {
            if (WaitForSingleObject(h[2], 15000) != WAIT_OBJECT_0)
                throw std::runtime_error("worker frame wait failed");
            MemoryBarrier();
            context->Draw(3, 0);
            context->Flush();
            check(device->GetDeviceRemovedReason(), "shared draw");
            MemoryBarrier();
            if (!SetEvent(h[3]))
                throw std::runtime_error("worker done event failed");
        }
        NrDdiStats stats;
        s.get(&stats);
        if (stats.executions != frames || stats.draws != frames || stats.rejected ||
            stats.inference_input_bytes || stats.host_upload_bytes || stats.host_copy_bytes ||
            stats.inference_output_bytes != uint64_t(width) * height * 4 * frames ||
            std::string(stats.device) != "NPU" || !stats.driver[0])
            throw std::runtime_error("worker execution attribution failed");
        context->ClearState();
        tv.Reset();
        sv.Reset();
        source.Reset();
        target.Reset();
        s.check_released("worker");
        s.stats("worker");
        return 0;
    } catch (const std::exception &e) {
        if (s.get)
            s.stats("worker");
        std::cout << "{\"worker_error\":" << quote(e.what()) << "}\n" << std::flush;
        return 1;
    }
}
unsigned byte_error(const D3D11_MAPPED_SUBRESOURCE &map, const std::vector<uint8_t> &fg,
                    const std::vector<uint8_t> &bg) {
    unsigned error = 0;
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
            for (size_t c = 0; c < 4; ++c) {
                const size_t i = (size_t(y) * width + x) * 4 + c;
                const double exact = fg[i] + bg[i] * (1.0 - fg[(i / 4) * 4 + 3] / 255.0);
                const int expected = int(std::nearbyint(std::clamp(exact, 0.0, 255.0)));
                const int actual =
                    static_cast<const uint8_t *>(map.pData)[size_t(y) * map.RowPitch + x * 4 + c];
                error = std::max(error, unsigned(std::abs(actual - expected)));
            }
    return error;
}
struct GpuReader {
    ComPtr<ID3D11Device> gpu;
    ComPtr<ID3D11DeviceContext> gpu_context;
    ComPtr<ID3D11Texture2D> imported, staging;
    void create() {
        ov::Core runtime;
        const auto identity = runtime.get_property("GPU", ov::device::luid);
        LUID luid{};
        static_assert(sizeof(luid) == ov::device::LUID::MAX_LUID_SIZE);
        std::memcpy(&luid, identity.luid.data(), sizeof(luid));
        ComPtr<IDXGIFactory4> factory;
        ComPtr<IDXGIAdapter1> adapter;
        check(CreateDXGIFactory1(IID_PPV_ARGS(&factory)), "GPU factory");
        check(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)), "exact GPU adapter");
        DXGI_ADAPTER_DESC1 ad{};
        check(adapter->GetDesc1(&ad), "GPU description");
        if (ad.AdapterLuid.HighPart != luid.HighPart || ad.AdapterLuid.LowPart != luid.LowPart)
            throw std::runtime_error("GPU identity mismatch");
        const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
        const UINT flags = GetEnvironmentVariableW(L"NR_DDI_GPU_DEBUG", nullptr, 0)
                               ? D3D11_CREATE_DEVICE_DEBUG
                               : 0;
        check(D3D11CreateDevice(adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags, &level, 1,
                                D3D11_SDK_VERSION, &gpu, nullptr, &gpu_context),
              "GPU D3D11 device");
    }
    HRESULT open(HANDLE shared) { return gpu->OpenSharedResource(shared, IID_PPV_ARGS(&imported)); }
    void prepare_readback() {
        D3D11_TEXTURE2D_DESC actual{};
        imported->GetDesc(&actual);
        if (actual.Width != width || actual.Height != height || actual.Format != format ||
            actual.MipLevels != 1 || actual.ArraySize != 1 || actual.SampleDesc.Count != 1)
            throw std::runtime_error("GPU imported texture description mismatch");
        auto desc = description(false);
        check(gpu->CreateTexture2D(&desc, nullptr, &staging), "GPU staging texture");
    }
    void write_control(const std::vector<uint8_t> &pixels, bool native = false) {
        if (native) {
            imported.Reset();
            auto desc = description(true);
            check(gpu->CreateTexture2D(&desc, nullptr, &imported), "native GPU control texture");
        }
        gpu_context->UpdateSubresource(imported.Get(), 0, nullptr, pixels.data(), width * 4,
                                       UINT(pixels.size()));
        gpu_context->CopyResource(staging.Get(), imported.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(gpu_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "GPU write control map");
        unsigned error = 0;
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width * 4; ++x)
                error = std::max(error,
                                 unsigned(std::abs(int(static_cast<const uint8_t *>(
                                                       map.pData)[size_t(y) * map.RowPitch + x]) -
                                                   int(pixels[size_t(y) * width * 4 + x]))));
        gpu_context->Unmap(staging.Get(), 0);
        std::cout << "{\"native_gpu_texture_control\":" << (native ? "true" : "false")
                  << ",\"gpu_write_control_error\":" << error << "}\n"
                  << std::flush;
    }
    unsigned read(const std::vector<uint8_t> &fg, const std::vector<uint8_t> &bg) {
        gpu_context->CopyResource(staging.Get(), imported.Get());
        D3D11_MAPPED_SUBRESOURCE map{};
        check(gpu_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map),
              "GPU shared texture readback");
        const auto error = byte_error(map, fg, bg);
        ComPtr<ID3D11InfoQueue> messages;
        if (SUCCEEDED(gpu.As(&messages))) {
            for (UINT64 i = 0; i < std::min<UINT64>(messages->GetNumStoredMessages(), 64); ++i) {
                SIZE_T size = 0;
                messages->GetMessage(i, nullptr, &size);
                if (size > 65536)
                    throw std::runtime_error("oversized GPU debug message");
                std::vector<uint8_t> storage(size);
                auto *message = reinterpret_cast<D3D11_MESSAGE *>(storage.data());
                if (SUCCEEDED(messages->GetMessage(i, message, &size)))
                    std::cout << "{\"gpu_debug_message\":" << quote(message->pDescription) << "}\n";
            }
            messages->ClearStoredMessages();
        }
        if (error > 1) {
            const auto *bytes = static_cast<const uint8_t *>(map.pData);
            std::cout << "{\"gpu_readback_row_pitch\":" << map.RowPitch << ",\"first_bytes\":[";
            for (size_t i = 0; i < 64; ++i)
                std::cout << (i ? "," : "") << unsigned(bytes[i]);
            size_t nonzero = 0;
            for (UINT y = 0; y < height; ++y)
                for (UINT x = 0; x < width * 4; ++x)
                    nonzero += bytes[size_t(y) * map.RowPitch + x] != 0;
            std::cout << "],\"nonzero_pixel_bytes\":" << nonzero << "}\n" << std::flush;
        }
        gpu_context->Unmap(staging.Get(), 0);
        check(gpu->GetDeviceRemovedReason(), "GPU readback device state");
        return error;
    }
};
int wmain(int argc, wchar_t **argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    if (argc == 11 && std::wstring(argv[1]) == L"--worker")
        return child_main(argv);
    if (argc != 2 && argc != 6 && argc != 7 && argc != 3) {
        std::cerr << "usage: nr_ddi_shared_probe absolute-shared-dll [width height frames "
                     "BGRA|RGBA [--gpu-readback] | --reject-width | --gpu-open]\n";
        return 2;
    }
    Session s;
    UINT tested = 0, maximum_error = 0, gpu_error = 0, gpu_frames = 0;
    bool gpu_readback = false;
    try {
        if (argc == 6 || argc == 7)
            dimensions(argv + 2);
        if (argc == 7) {
            if (std::wstring(argv[6]) != L"--gpu-readback")
                throw std::runtime_error("unknown GPU readback option");
            gpu_readback = true;
            if (!((width == 64 && height == 48) || (width == 320 && height == 240)))
                throw std::runtime_error("native GPU import diagnostic is limited to small "
                                         "fixtures; larger imports crashed the process");
        }
        const bool gpu_open = argc == 3 && std::wstring(argv[2]) == L"--gpu-open";
        if (argc == 3 && std::wstring(argv[2]) != L"--reject-width" && !gpu_open)
            throw std::runtime_error("unknown rejection case");
        std::cout << "{\"width\":" << width << ",\"height\":" << height
                  << ",\"requested_frames\":" << frames << ",\"format\":" << uint32_t(format)
                  << "}\n";
        auto dll = std::filesystem::canonical(argv[1]).wstring();
        if (dll.find(L'"') != std::wstring::npos)
            throw std::runtime_error("invalid DLL path");
        s.create(dll);
        auto desc = description(true);
        ComPtr<ID3D11Texture2D> source, target, staging;
        if (argc == 3 && !gpu_open) {
            desc.Width = 63;
            const auto result = s.device->CreateTexture2D(&desc, nullptr, &source);
            NrDdiStats stats;
            s.get(&stats);
            if (SUCCEEDED(result) || source || !stats.rejected || stats.executions)
                throw std::runtime_error("unaligned shared texture was not rejected");
            s.check_released("parent");
            s.stats("parent");
            std::cout << "{\"passed\":true,\"rejection_confirmed\":true,\"full_desktop_offload\":"
                         "false}\n";
            return 0;
        }
        check(s.device->CreateTexture2D(&desc, nullptr, &source), "create shared source");
        check(s.device->CreateTexture2D(&desc, nullptr, &target), "create shared target");
        std::array<HANDLE, 2> shared{};
        size_t index = 0;
        for (auto *t : {source.Get(), target.Get()}) {
            ComPtr<IDXGIResource> resource;
            check(t->QueryInterface(IID_PPV_ARGS(&resource)), "shared resource interface");
            check(resource->GetSharedHandle(&shared[index]), "GetSharedHandle");
            if (!shared[index++])
                throw std::runtime_error("empty shared resource handle");
        }
        std::cout << "{\"shared_textures_created\":true,\"legacy_handles_"
                     "returned\":true}\n"
                  << std::flush;
        if (gpu_open) {
            GpuReader gpu;
            gpu.create();
            const HRESULT status = gpu.open(shared[0]);
            std::cout << "{\"gpu_open_completed\":true,\"gpu_open_hresult\":" << uint32_t(status)
                      << ",\"gpu_open_succeeded\":" << (SUCCEEDED(status) ? "true" : "false")
                      << ",\"full_desktop_offload\":false}\n";
            // This diagnostic reports capability; a successful open alone would
            // still require separate layout/coherence and presentation checks.
            return 0;
        }
        desc = description(false);
        check(s.device->CreateTexture2D(&desc, nullptr, &staging), "create readback");
        Worker worker;
        worker.launch(shared, dll);
        // Only the importing child retains the allocations during this handoff.
        // Then the original process reopens the real legacy handles via D3D11.
        source.Reset();
        target.Reset();
        s.check_released("parent-before-reopen");
        check(s.device->OpenSharedResource(shared[0], IID_PPV_ARGS(&source)), "reopen source");
        check(s.device->OpenSharedResource(shared[1], IID_PPV_ARGS(&target)), "reopen target");
        std::cout << "{\"creator_released_and_reopened\":true}\n" << std::flush;
        GpuReader gpu;
        if (gpu_readback) {
            gpu.create();
            check(gpu.open(shared[1]), "GPU open NPU target texture");
            gpu.prepare_readback();
            std::cout << "{\"gpu_adapter_matches_openvino\":true,\"gpu_texture_imported\":true}\n"
                      << std::flush;
        }
        std::vector<uint8_t> fg(size_t(width) * height * 4), bg(fg.size());
        for (UINT frame = 0; frame < frames; ++frame) {
            for (size_t i = 0; i < fg.size(); i += 4) {
                const unsigned alpha = (i / 4 * 37 + frame * 19) % 256;
                for (size_t c = 0; c < 3; ++c) {
                    fg[i + c] = uint8_t((i / 4 + c * 23 + frame * 71) % (alpha + 1));
                    bg[i + c] = uint8_t((i / 4 * 7 + c * 53 + frame * 31) % 256);
                }
                fg[i + 3] = uint8_t(alpha);
                bg[i + 3] = 255;
            }
            s.context->UpdateSubresource(source.Get(), 0, nullptr, fg.data(), width * 4,
                                         UINT(fg.size()));
            s.context->UpdateSubresource(target.Get(), 0, nullptr, bg.data(), width * 4,
                                         UINT(bg.size()));
            s.context->Flush();
            check(s.device->GetDeviceRemovedReason(), "parent update");
            MemoryBarrier();
            worker.infer();
            MemoryBarrier();
            s.context->CopyResource(staging.Get(), target.Get());
            D3D11_MAPPED_SUBRESOURCE map{};
            check(s.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &map), "parent readback");
            const unsigned error = byte_error(map, fg, bg);
            s.context->Unmap(staging.Get(), 0);
            maximum_error = std::max(maximum_error, error);
            if (error > 1)
                throw std::runtime_error("cross-process pixel oracle failed");
            if (gpu_readback) {
                gpu_error = std::max(gpu_error, gpu.read(fg, bg));
                if (gpu_error > 1) {
                    gpu.write_control(fg);
                    s.context->CopyResource(staging.Get(), target.Get());
                    D3D11_MAPPED_SUBRESOURCE control{};
                    check(s.context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &control),
                          "NPU mapping of GPU write control");
                    unsigned error = 0;
                    for (UINT y = 0; y < height; ++y)
                        for (UINT x = 0; x < width * 4; ++x)
                            error = std::max(
                                error, unsigned(std::abs(
                                           int(static_cast<const uint8_t *>(
                                               control.pData)[size_t(y) * control.RowPitch + x]) -
                                           int(fg[size_t(y) * width * 4 + x]))));
                    s.context->Unmap(staging.Get(), 0);
                    std::cout << "{\"cpu_mapping_gpu_write_control_error\":" << error << "}\n"
                              << std::flush;
                    gpu.write_control(fg, true);
                    throw std::runtime_error("GPU shared texture pixel oracle failed");
                }
                ++gpu_frames;
            }
            ++tested;
        }
        worker.finish();
        source.Reset();
        target.Reset();
        staging.Reset();
        s.context->ClearState();
        s.check_released("parent");
        s.stats("parent");
        std::cout << "{\"passed\":true,\"full_desktop_offload\":false,\"frames\":" << tested
                  << ",\"maximum_byte_error\":" << maximum_error
                  << ",\"gpu_readback_requested\":" << (gpu_readback ? "true" : "false")
                  << ",\"gpu_readback_frames\":" << gpu_frames
                  << ",\"gpu_maximum_byte_error\":" << gpu_error << "}\n";
        return 0;
    } catch (const std::exception &e) {
        if (s.get)
            s.stats("parent");
        std::cout << "{\"passed\":false,\"full_desktop_offload\":false,\"frames\":" << tested
                  << ",\"maximum_byte_error\":" << maximum_error
                  << ",\"gpu_readback_requested\":" << (gpu_readback ? "true" : "false")
                  << ",\"gpu_readback_frames\":" << gpu_frames
                  << ",\"gpu_maximum_byte_error\":" << gpu_error << ",\"error\":" << quote(e.what())
                  << "}\n";
        return 1;
    }
}
