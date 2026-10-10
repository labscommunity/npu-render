// Ordinary registered-adapter D3D11 calls, restricted to the disposable guest.
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_4.h>
#include <d3dkmthk.h>
#include <wrl/client.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <stdexcept>
#include <vector>
#include "contract.hpp"
#include "../kmd/contract.hpp"
#include "shader_contract.hpp"
using Microsoft::WRL::ComPtr;
void check(HRESULT status, const char *operation) {
    std::printf("{\"operation\":\"%s\",\"status\":\"%08lx\"}\n", operation, ULONG(status));
    std::fflush(stdout);
    if (FAILED(status)) throw std::runtime_error(operation);
}
void require(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
LUID select_adapter(WCHAR (&umd)[MAX_PATH]) {
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
            info.bytes == expected.bytes && info.ready == 0) {
            ++matches; selected = a.AdapterLuid;
            D3DKMT_UMDFILENAMEINFO name{};
            name.Version = KMTUMDVERSION_DX10;
            query.Type = KMTQAITYPE_UMDRIVERNAME;
            query.pPrivateDriverData = &name; query.PrivateDriverDataSize = sizeof(name);
            require(D3DKMTQueryAdapterInfo(&query) == 0 && std::wmemchr(name.UmdFileName, 0, MAX_PATH), "registered UMD path query");
            std::memcpy(umd, name.UmdFileName, sizeof(umd));
        }
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
struct KeyedAccess {
    IDXGIKeyedMutex *mutex = nullptr;
    KeyedAccess(IDXGIKeyedMutex *m, UINT64 key) : mutex(m) {
        if (mutex) {
            const auto status = mutex->AcquireSync(key, 10000);
            check(status, "acquire_keyed_mutex");
            require(status == S_OK, "keyed mutex timed out or was abandoned");
        }
    }
    ~KeyedAccess() { if (mutex) mutex->ReleaseSync(0); }
    void release(UINT64 key) {
        if (mutex) { check(mutex->ReleaseSync(key), "release_keyed_mutex"); mutex = nullptr; }
    }
};
void exercise_shared(IDXGIAdapter *adapter, ID3D11Device *creator, ID3D11DeviceContext *creator_context,
                     DXGI_FORMAT format, bool nt_handles = false) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 48; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = nt_handles ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX : D3D11_RESOURCE_MISC_SHARED;
    ComPtr<ID3D11Texture2D> original;
    check(creator->CreateTexture2D(&desc, nullptr, &original), "create_shared_target");
    ComPtr<IDXGIResource> identity;
    check(original.As(&identity), "query_shared_identity");
    HANDLE shared = nullptr;
    struct OwnedShare { HANDLE value = nullptr; ~OwnedShare() { if (value) CloseHandle(value); } } owned;
    if (nt_handles) {
        ComPtr<IDXGIResource1> resource;
        check(identity.As(&resource), "query_nt_shared_identity");
        check(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &owned.value), "create_nt_shared_handle");
        shared = owned.value;
    } else check(identity->GetSharedHandle(&shared), "get_shared_handle");
    require(shared != nullptr, "missing shared handle"); // Only NT handles are CloseHandle-owned.
    ComPtr<ID3D11Device> consumer;
    ComPtr<ID3D11DeviceContext> consumer_context;
    const D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_10_0;
    check(D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, &level, 1, D3D11_SDK_VERSION,
                           &consumer, nullptr, &consumer_context), "create_consumer_device");
    ComPtr<ID3D11Texture2D> imported, reopened, staging;
    const auto open = [&](ID3D11Device *device, ComPtr<ID3D11Texture2D> &texture, const char *operation) {
        if (nt_handles) {
            ComPtr<ID3D11Device1> extended;
            check(device->QueryInterface(IID_PPV_ARGS(&extended)), "query_device1");
            check(extended->OpenSharedResource1(shared, IID_PPV_ARGS(&texture)), operation);
        } else check(device->OpenSharedResource(shared, IID_PPV_ARGS(&texture)), operation);
    };
    open(consumer.Get(), imported, "open_shared_target");
    D3D11_TEXTURE2D_DESC actual{};
    imported->GetDesc(&actual);
    require(actual.Width == desc.Width && actual.Height == desc.Height && actual.Format == format &&
            actual.BindFlags == desc.BindFlags && actual.MiscFlags == desc.MiscFlags, "imported texture description");
    identity.Reset(); original.Reset();
    std::puts("{\"creator_resource_released\":true,\"consumer_still_owns_resource\":true}");
    open(creator, reopened, "reopen_after_creator_release");
    ComPtr<IDXGIKeyedMutex> consumer_mutex, creator_mutex;
    if (nt_handles) {
        check(imported.As(&consumer_mutex), "consumer_keyed_mutex");
        check(reopened.As(&creator_mutex), "creator_keyed_mutex");
    }
    ComPtr<ID3D11RenderTargetView> consumer_view, creator_view;
    check(consumer->CreateRenderTargetView(imported.Get(), nullptr, &consumer_view), "create_imported_view");
    check(creator->CreateRenderTargetView(reopened.Get(), nullptr, &creator_view), "create_reopened_view");
    desc.MiscFlags = desc.BindFlags = 0; desc.Usage = D3D11_USAGE_STAGING; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    check(consumer->CreateTexture2D(&desc, nullptr, &staging), "create_shared_staging");
    const FLOAT colors[3][4] = {{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}};
    for (UINT pass = 0; pass < 3; ++pass) {
        auto writer = pass == 1 ? creator_context : consumer_context.Get();
        auto view = pass == 1 ? creator_view.Get() : consumer_view.Get();
        KeyedAccess write_access(pass == 1 ? creator_mutex.Get() : consumer_mutex.Get(), 0);
        writer->ClearRenderTargetView(view, colors[pass]);
        writer->Flush();
        check((pass == 1 ? creator : consumer.Get())->GetDeviceRemovedReason(), "shared_clear_device_status");
        write_access.release(1);
        KeyedAccess read_access(consumer_mutex.Get(), 1);
        consumer_context->CopyResource(staging.Get(), imported.Get());
        check(consumer->GetDeviceRemovedReason(), "shared_copy_device_status");
        D3D11_MAPPED_SUBRESOURCE mapped{};
        check(consumer_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped), "shared_map");
        bool valid = mapped.pData && mapped.RowPitch >= desc.Width * 4;
        for (UINT y = 0; valid && y < desc.Height; ++y) {
            const auto row = static_cast<const unsigned char *>(mapped.pData) + size_t(y) * mapped.RowPitch;
            for (UINT x = 0; valid && x < desc.Width; ++x) for (UINT c = 0; c < 4; ++c) {
                const auto channel = format == DXGI_FORMAT_B8G8R8A8_UNORM && c != 1 && c != 3 ? 2 - c : c;
                if (row[x * 4 + c] != (channel == pass || channel == 3 ? 255 : 0)) { valid = false; break; }
            }
        }
        consumer_context->Unmap(staging.Get(), 0);
        check(consumer->GetDeviceRemovedReason(), "shared_unmap_device_status");
        read_access.release(0);
        require(valid, "shared pixel mismatch");
        std::printf("{\"shared_format\":%u,\"pass\":%u,\"verified_bytes\":12288,\"same_process\":true,\"nt_handles\":%s}\n", UINT(format), pass, nt_handles ? "true" : "false");
    }
}
#include "process_probe.hpp"
#include "upload_probe.hpp"
#include "draw_probe.hpp"
#include "present_probe.hpp"
int main(int argc, char **argv) {
    const char *present_backend = argc == 2 && !std::strcmp(argv[1], "--present-draw-cpu") ? "CPU" :
                                  (argc == 2 && !std::strcmp(argv[1], "--present-draw-npu") ? "NPU" : nullptr);
    const bool presentation = present_backend || (argc == 2 && !std::strcmp(argv[1], "--present"));
    const bool child_nt = argc == 5 && std::strcmp(argv[1], "--shared-child-nt") == 0;
    const bool child = child_nt || (argc == 5 && std::strcmp(argv[1], "--shared-child") == 0);
    const bool process_nt = argc == 2 && std::strcmp(argv[1], "--shared-process-nt") == 0;
    const bool processes = process_nt || (argc == 2 && std::strcmp(argv[1], "--shared-process") == 0);
    const bool nt_handles = argc == 2 && std::strcmp(argv[1], "--shared-nt") == 0;
    const bool uploads = argc == 2 && std::strcmp(argv[1], "--upload") == 0;
    const bool desktop_size = argc == 3 && !std::strcmp(argv[2], "--desktop-size");
    const bool draw_args = argc == 2 || desktop_size;
    const char *draw_backend = present_backend ? present_backend :
                               (draw_args && !std::strcmp(argv[1], "--draw-cpu") ? "CPU" :
                               (draw_args && !std::strcmp(argv[1], "--draw-npu") ? "NPU" : nullptr));
    if (!presentation && !child && !processes && !nt_handles && !uploads && !draw_backend && (argc != 2 || (std::strcmp(argv[1], "--exercise") && std::strcmp(argv[1], "--shared")))) return 2;
    const bool shared = nt_handles || std::strcmp(argv[1], "--shared") == 0;
    if (!nr_fill_guest()) { std::puts("{\"passed\":false,\"error\":\"owned graphics guest required\"}"); return 2; }
    HMODULE retained = nullptr;
    struct RuntimeDirectory {
        DLL_DIRECTORY_COOKIE cookie = nullptr;
        ~RuntimeDirectory() { if (cookie) RemoveDllDirectory(cookie); }
    } runtime_directory;
    try {
        WCHAR runtime[MAX_PATH]{};
        const auto count = GetEnvironmentVariableW(L"NR_REGISTERED_RUNTIME", runtime, MAX_PATH);
        require(count && count < MAX_PATH && SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS), "explicit OpenVINO runtime directory required");
        runtime_directory.cookie = AddDllDirectory(runtime);
        require(runtime_directory.cookie != nullptr, "OpenVINO runtime directory unavailable");
        WCHAR umd[MAX_PATH]{}, root[MAX_PATH]{};
        const auto luid = select_adapter(umd);
        require(GetWindowsDirectoryW(root, MAX_PATH) != 0, "Windows directory");
        require(wcscat_s(root, L"\\System32\\DriverStore\\FileRepository\\") == 0, "driver-store prefix");
        const auto name = std::wcsrchr(umd, L'\\');
        require(_wcsnicmp(umd, root, std::wcslen(root)) == 0 && !std::wcsstr(umd, L"\\..") &&
                name && _wcsicmp(name + 1, L"nr_fill_umd.dll") == 0, "unexpected registered UMD path");
        // Retain the exact OS-registered DLL for diagnostics even if device
        // initialization fails and the runtime releases its own module handle.
        // D3D11 still uses the selected adapter and a null software-module input.
        retained = LoadLibraryExW(umd, nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_USER_DIRS);
        require(retained != nullptr, "retain registered UMD diagnostics");
        require(!GetProcAddress(retained, "D3DKMTCreateDevice"), "unexpected KMT replacement export");
        if (draw_backend) {
            const auto configure = reinterpret_cast<BOOL (*)(const char *)>(GetProcAddress(retained, "nr_fill_configure"));
            require(configure && configure(draw_backend), "explicit draw backend configuration failed");
        }
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
        auto stats = reinterpret_cast<BOOL (*)(NrFillStats *)>(GetProcAddress(retained, "nr_fill_stats"));
        require(stats != nullptr, "UMD diagnostics unavailable");
        if (presentation) exercise_present(factory.Get(), device.Get(), context.Get(), present_backend != nullptr);
        else if (draw_backend) {
            const std::array<std::pair<UINT, UINT>, 2> extents = desktop_size ?
                std::array<std::pair<UINT, UINT>, 2>{{{1920, 1080}, {2880, 1800}}} :
                std::array<std::pair<UINT, UINT>, 2>{{{64, 16}, {132, 16}}};
            for (const auto format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM})
                for (const auto &[width, height] : extents) for (UINT sharing = 0; sharing < 3; ++sharing)
                    exercise_draw(device.Get(), context.Get(), format, width, height, sharing);
        } else if (uploads) {
            for (const auto format : {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UNORM})
                for (UINT sharing = 0; sharing < 3; ++sharing) exercise_upload(device.Get(), context.Get(), format, sharing);
        } else if (child) process_child(device.Get(), context.Get(), argv, child_nt);
        else if (processes) {
            exercise_process(device.Get(), context.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, process_nt);
            exercise_process(device.Get(), context.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, process_nt);
        } else if (shared) {
            exercise_shared(adapter.Get(), device.Get(), context.Get(), DXGI_FORMAT_B8G8R8A8_UNORM, nt_handles);
            exercise_shared(adapter.Get(), device.Get(), context.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, nt_handles);
        } else {
            exercise(device.Get(), context.Get(), DXGI_FORMAT_B8G8R8A8_UNORM);
            exercise(device.Get(), context.Get(), DXGI_FORMAT_R8G8B8A8_UNORM);
        }
        context.Reset(); device.Reset(); adapter.Reset(); factory.Reset();
        NrFillStats result{};
        require(stats(&result) != FALSE, "UMD diagnostics failed");
        std::printf("{\"present_requests\":%llu,\"accepted_present_callbacks\":%llu,\"display_delivery_tested\":%s}\n", result.present_requests, result.accepted_present_callbacks, presentation ? "true" : "false");
        std::printf("{\"pid\":%lu,\"allocations\":%llu,\"fills\":%llu,\"staging_copy_bytes\":%llu,\"live_allocations\":%llu,\"live_contexts\":%llu,\"cleanup_errors\":%llu,\"rejected\":%llu,\"submitted_copies\":%llu,\"upload_bytes\":%llu}\n", GetCurrentProcessId(),
                    result.allocations, result.fills, result.staging_copy_bytes, result.live_allocations,
                    result.live_contexts, result.cleanup_errors, result.rejected, result.submitted_copies, result.upload_bytes);
        const UINT copies = child ? 2 : (processes ? 4 : 6);
        std::printf("{\"draws\":%llu,\"executions\":%llu,\"cpu_executions\":%llu,\"gpu_executions\":%llu,\"npu_executions\":%llu,\"execution_device\":\"%s\",\"inference_input_bytes\":%llu,\"inference_output_bytes\":%llu}\n",
                    result.draws, result.executions, result.cpu_executions, result.gpu_executions, result.npu_executions,
                    result.last_execution_device, result.inference_input_bytes, result.inference_output_bytes);
        if (presentation) {
            require(result.fills == 3 && result.present_requests == 3 && result.accepted_present_callbacks == 3, "unexpected presentation counters");
            if (present_backend) require(result.draws == 3 && result.executions == 3 && !result.gpu_executions &&
                    result.cpu_executions == (!std::strcmp(present_backend, "CPU") ? 3u : 0u) &&
                    result.npu_executions == (!std::strcmp(present_backend, "NPU") ? 3u : 0u), "unexpected presented draw backend");
        }
        else if (draw_backend) {
            require(result.draws == 24 && result.executions == 24 && !result.gpu_executions &&
                    result.cpu_executions == (!std::strcmp(draw_backend, "CPU") ? 24u : 0u) &&
                    result.npu_executions == (!std::strcmp(draw_backend, "NPU") ? 24u : 0u), "unexpected draw execution counters");
            require(result.allocations == 100 && !result.fills && result.submitted_copies == 88 &&
                    result.staging_copy_bytes == (desktop_size ? 1393557504ull : 786432ull) &&
                    result.upload_bytes == (desktop_size ? 740275200ull : 319872ull) &&
                    result.inference_input_bytes == (desktop_size ? 696729600ull : 301056ull) &&
                    result.inference_output_bytes == (desktop_size ? 348364800ull : 150528ull),
                    "unexpected draw traffic counters");
        } else if (uploads) require(result.allocations == 24 && !result.fills && result.staging_copy_bytes == 819200 &&
                             result.submitted_copies == 32 && result.upload_bytes == 119496, "unexpected upload counters");
        else require(result.allocations == (child ? 1u : 4u) && result.fills == (child ? 1u : (processes ? 2u : 6u)) &&
                result.staging_copy_bytes == copies * 12288 && result.submitted_copies == (shared || child || processes ? copies : 0u) &&
                !result.upload_bytes, "unexpected UMD counters");
        require(!result.live_allocations && !result.live_contexts && !result.cleanup_errors && !result.rejected, "unexpected cleanup counters");
        if (!draw_backend) require(!result.executions && !result.draws, "unexpected rendering execution");
        FreeLibrary(retained);
        std::printf("{\"passed\":true,\"npu_execution_tested\":%s,\"desktop_offload_tested\":false}\n", result.npu_executions ? "true" : "false");
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "registered clear failed: %s\n", e.what());
        if (retained) {
            auto stats = reinterpret_cast<BOOL (*)(NrFillStats *)>(GetProcAddress(retained, "nr_fill_stats"));
            NrFillStats result{};
            if (stats && stats(&result))
                std::printf("{\"failed\":true,\"rejected\":%llu,\"last_unsupported_offset\":%llu,\"cleanup_errors\":%llu,\"executions\":%llu,\"cpu_executions\":%llu,\"gpu_executions\":%llu,\"npu_executions\":%llu,\"live_allocations\":%llu,\"live_contexts\":%llu,\"present_requests\":%llu,\"accepted_present_callbacks\":%llu}\n",
                            result.rejected, result.last_unsupported_offset, result.cleanup_errors, result.executions,
                            result.cpu_executions, result.gpu_executions, result.npu_executions, result.live_allocations, result.live_contexts, result.present_requests, result.accepted_present_callbacks);
            FreeLibrary(retained);
        }
        return 1;
    }
}
