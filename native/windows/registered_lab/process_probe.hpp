// Cross-process lifetime and ordered-writer test. All handles and child
// processes belong to this guest-only probe; no driver installation occurs.
#pragma once
#include <string>
#include <cstdlib>
#include <cerrno>
struct ProbeHandle {
    HANDLE value = nullptr;
    ProbeHandle() = default;
    explicit ProbeHandle(HANDLE h) : value(h) {}
    ProbeHandle(const ProbeHandle &) = delete;
    ProbeHandle &operator=(const ProbeHandle &) = delete;
    ~ProbeHandle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct ProbeEvents {
    ProbeHandle ready, go, done, finish;
    ProbeEvents(const std::wstring &prefix, bool create) {
        const wchar_t *suffixes[] = {L"_ready", L"_go", L"_done", L"_finish"};
        ProbeHandle *handles[] = {&ready, &go, &done, &finish};
        for (UINT i = 0; i < 4; ++i) {
            const auto name = prefix + suffixes[i];
            if (create) {
                handles[i]->value = CreateEventW(nullptr, TRUE, FALSE, name.c_str());
                const auto error = GetLastError();
                require(handles[i]->value && error != ERROR_ALREADY_EXISTS, "unique probe event");
            } else {
                handles[i]->value = OpenEventW(SYNCHRONIZE | EVENT_MODIFY_STATE, FALSE, name.c_str());
                require(handles[i]->value != nullptr, "open probe event");
            }
        }
    }
};
struct ProbeChild {
    ProbeHandle process, thread;
    HANDLE child_shared = nullptr;
    ~ProbeChild() {
        if (process.value && WaitForSingleObject(process.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(process.value, 99); // Only the exact child created below.
            WaitForSingleObject(process.value, 10000);
        }
    }
    void start(HANDLE shared, DXGI_FORMAT format, const std::wstring &prefix, bool nt_handles) {
        WCHAR executable[MAX_PATH]{};
        const auto n = GetModuleFileNameW(nullptr, executable, MAX_PATH);
        require(n && n < MAX_PATH, "probe executable path");
        ProbeHandle transfer;
        const auto self = GetCurrentProcess();
        if (nt_handles) {
            require(DuplicateHandle(self, shared, self, &transfer.value, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE,
                    "inheritable NT resource handle");
            child_shared = transfer.value;
        } else child_shared = shared;
        WCHAR parameters[512]{};
        require(swprintf_s(parameters, L" %s %llx %u %s", nt_handles ? L"--shared-child-nt" : L"--shared-child",
                          ULONGLONG(ULONG_PTR(child_shared)), UINT(format), prefix.c_str()) > 0,
                "child arguments");
        std::wstring command = L"\"" + std::wstring(executable) + L"\"" + parameters;
        ProbeHandle output, errors, input;
        require(DuplicateHandle(self, GetStdHandle(STD_OUTPUT_HANDLE), self, &output.value, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE,
                "child stdout handle");
        require(DuplicateHandle(self, GetStdHandle(STD_ERROR_HANDLE), self, &errors.value, 0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE,
                "child stderr handle");
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        input.value = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
        require(input.value != INVALID_HANDLE_VALUE, "child stdin handle");
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> storage(bytes);
        auto attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
        require(InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != FALSE, "child handle list");
        struct AttributeCleanup {
            LPPROC_THREAD_ATTRIBUTE_LIST value;
            ~AttributeCleanup() { DeleteProcThreadAttributeList(value); }
        } cleanup{attributes};
        HANDLE handles[] = {input.value, output.value, errors.value, transfer.value};
        require(UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles,
                                           (nt_handles ? 4 : 3) * sizeof(HANDLE), nullptr, nullptr) != FALSE,
                "restrict inherited handles");
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = input.value; startup.StartupInfo.hStdOutput = output.value;
        startup.StartupInfo.hStdError = errors.value; startup.lpAttributeList = attributes;
        PROCESS_INFORMATION created{};
        require(CreateProcessW(executable, command.data(), nullptr, nullptr, TRUE,
                               EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, nullptr,
                               &startup.StartupInfo, &created) != FALSE, "create child probe");
        process.value = created.hProcess; thread.value = created.hThread;
        std::printf("{\"parent_pid\":%lu,\"child_pid\":%lu,\"cross_process\":true,\"nt_handles\":%s}\n",
                    GetCurrentProcessId(), created.dwProcessId, nt_handles ? "true" : "false");
        std::fflush(stdout);
    }
    void wait(HANDLE event) {
        const HANDLE handles[] = {event, process.value};
        require(WaitForMultipleObjects(2, handles, FALSE, 120000) == WAIT_OBJECT_0, "child event or early exit");
    }
    void finish() {
        require(WaitForSingleObject(process.value, 120000) == WAIT_OBJECT_0, "child exit timeout");
        DWORD code = 0;
        require(GetExitCodeProcess(process.value, &code) && !code, "child validation failed");
        std::puts("{\"child_exit\":0,\"cross_process\":true}");
    }
};
ComPtr<ID3D11Texture2D> process_staging(ID3D11Device *device, DXGI_FORMAT format) {
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 48; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1; desc.Usage = D3D11_USAGE_STAGING;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> texture;
    check(device->CreateTexture2D(&desc, nullptr, &texture), "process_create_staging");
    return texture;
}
void process_pixels(ID3D11Device *device, ID3D11DeviceContext *context, ID3D11Texture2D *source,
                    ID3D11Texture2D *staging, DXGI_FORMAT format, UINT color) {
    context->CopyResource(staging, source);
    check(device->GetDeviceRemovedReason(), "process_copy_status");
    D3D11_MAPPED_SUBRESOURCE mapped{};
    check(context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped), "process_map");
    bool valid = mapped.pData && mapped.RowPitch >= 256;
    for (UINT y = 0; valid && y < 48; ++y) {
        const auto row = static_cast<const unsigned char *>(mapped.pData) + size_t(y) * mapped.RowPitch;
        for (UINT x = 0; valid && x < 64; ++x) for (UINT c = 0; c < 4; ++c) {
            const UINT channel = format == DXGI_FORMAT_B8G8R8A8_UNORM && c != 1 && c != 3 ? 2 - c : c;
            if (row[x * 4 + c] != (channel == color || channel == 3 ? 255 : 0)) { valid = false; break; }
        }
    }
    context->Unmap(staging, 0);
    check(device->GetDeviceRemovedReason(), "process_unmap_status");
    require(valid, "cross-process pixels differ");
    std::printf("{\"pid\":%lu,\"cross_process_format\":%u,\"color\":%u,\"verified_bytes\":12288}\n",
                GetCurrentProcessId(), UINT(format), color);
    std::fflush(stdout);
}
void process_open(ID3D11Device *device, HANDLE shared, ComPtr<ID3D11Texture2D> &texture, bool nt_handles) {
    if (nt_handles) {
        ComPtr<ID3D11Device1> extended;
        check(device->QueryInterface(IID_PPV_ARGS(&extended)), "process_query_device1");
        check(extended->OpenSharedResource1(shared, IID_PPV_ARGS(&texture)), "process_open_nt_shared");
    } else check(device->OpenSharedResource(shared, IID_PPV_ARGS(&texture)), "process_open_shared");
}
void process_child(ID3D11Device *device, ID3D11DeviceContext *context, char **argv, bool nt_handles) {
    char *end = nullptr; errno = 0;
    const auto raw = std::strtoull(argv[2], &end, 16);
    require(!errno && raw && end && !*end && raw <= UINTPTR_MAX, "child shared handle");
    const DXGI_FORMAT format = std::strcmp(argv[3], "87") == 0 ? DXGI_FORMAT_B8G8R8A8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
    require(std::strcmp(argv[3], "87") == 0 || std::strcmp(argv[3], "28") == 0, "child texture format");
    const std::string name(argv[4]); const std::string begin = "Local\\NpuRenderShared_";
    require(name.rfind(begin, 0) == 0 && name.size() < 128 && name.size() > begin.size() &&
            name.find_first_not_of("0123456789_", begin.size()) == std::string::npos, "child event prefix");
    ProbeEvents events(std::wstring(name.begin(), name.end()), false);
    ProbeHandle owned(nt_handles ? reinterpret_cast<HANDLE>(ULONG_PTR(raw)) : nullptr);
    ComPtr<ID3D11Texture2D> imported;
    process_open(device, reinterpret_cast<HANDLE>(ULONG_PTR(raw)), imported, nt_handles);
    D3D11_TEXTURE2D_DESC desc{}; imported->GetDesc(&desc);
    require(desc.Width == 64 && desc.Height == 48 && desc.Format == format && desc.MipLevels == 1 &&
            desc.ArraySize == 1 && desc.SampleDesc.Count == 1 && desc.BindFlags == D3D11_BIND_RENDER_TARGET &&
            desc.MiscFlags == UINT(nt_handles ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX : D3D11_RESOURCE_MISC_SHARED),
            "cross-process descriptor");
    ComPtr<IDXGIKeyedMutex> mutex;
    if (nt_handles) check(imported.As(&mutex), "process_child_keyed_mutex");
    auto staging = process_staging(device, format);
    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(imported.Get(), nullptr, &view), "process_create_view");
    require(SetEvent(events.ready.value) != FALSE, "signal child ready");
    require(WaitForSingleObject(events.go.value, 120000) == WAIT_OBJECT_0, "wait for parent write");
    KeyedAccess access(mutex.Get(), 1);
    process_pixels(device, context, imported.Get(), staging.Get(), format, 0);
    const FLOAT blue[4] = {0, 0, 1, 1}; context->ClearRenderTargetView(view.Get(), blue); context->Flush();
    check(device->GetDeviceRemovedReason(), "child_clear_status");
    process_pixels(device, context, imported.Get(), staging.Get(), format, 2);
    access.release(2);
    require(SetEvent(events.done.value) != FALSE, "signal child write");
    require(WaitForSingleObject(events.finish.value, 120000) == WAIT_OBJECT_0, "wait for parent verification");
}
void exercise_process(ID3D11Device *device, ID3D11DeviceContext *context, DXGI_FORMAT format, bool nt_handles) {
    WCHAR prefix[128]{};
    require(swprintf_s(prefix, L"Local\\NpuRenderShared_%lu_%llu_%u", GetCurrentProcessId(), GetTickCount64(), UINT(format)) > 0,
            "event prefix");
    ProbeEvents events(prefix, true);
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = 64; desc.Height = 48; desc.MipLevels = desc.ArraySize = 1;
    desc.Format = format; desc.SampleDesc.Count = 1;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    desc.MiscFlags = nt_handles ? D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX : D3D11_RESOURCE_MISC_SHARED;
    ComPtr<ID3D11Texture2D> original, reopened;
    check(device->CreateTexture2D(&desc, nullptr, &original), "process_create_shared");
    ComPtr<IDXGIResource> identity; check(original.As(&identity), "process_shared_identity");
    HANDLE shared = nullptr;
    ProbeHandle owned;
    if (nt_handles) {
        ComPtr<IDXGIResource1> resource;
        check(identity.As(&resource), "process_nt_identity");
        check(resource->CreateSharedHandle(nullptr, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE, nullptr, &owned.value), "process_create_nt_handle");
        shared = owned.value;
    } else check(identity->GetSharedHandle(&shared), "process_shared_handle");
    auto staging = process_staging(device, format);
    ProbeChild child; child.start(shared, format, prefix, nt_handles); child.wait(events.ready.value);
    if (nt_handles) { require(CloseHandle(owned.value) != FALSE, "release original NT handle"); owned.value = nullptr; }
    identity.Reset(); original.Reset(); // Child is now the sole resource owner.
    if (nt_handles) {
        require(DuplicateHandle(child.process.value, child.child_shared, GetCurrentProcess(), &owned.value,
                                0, FALSE, DUPLICATE_SAME_ACCESS) != FALSE, "recover NT handle from child owner");
        shared = owned.value;
    }
    process_open(device, shared, reopened, nt_handles);
    std::puts("{\"process_reopened_after_creator_release\":true}");
    ComPtr<IDXGIKeyedMutex> mutex;
    if (nt_handles) check(reopened.As(&mutex), "process_parent_keyed_mutex");
    ComPtr<ID3D11RenderTargetView> view;
    check(device->CreateRenderTargetView(reopened.Get(), nullptr, &view), "process_reopened_view");
    KeyedAccess write_access(mutex.Get(), 0);
    const FLOAT red[4] = {1, 0, 0, 1}; context->ClearRenderTargetView(view.Get(), red); context->Flush();
    check(device->GetDeviceRemovedReason(), "parent_clear_status");
    process_pixels(device, context, reopened.Get(), staging.Get(), format, 0);
    write_access.release(1);
    require(SetEvent(events.go.value) != FALSE, "signal parent write");
    child.wait(events.done.value);
    KeyedAccess read_access(mutex.Get(), 2);
    process_pixels(device, context, reopened.Get(), staging.Get(), format, 2);
    read_access.release(0);
    require(SetEvent(events.finish.value) != FALSE, "signal parent verification");
    child.finish();
}
